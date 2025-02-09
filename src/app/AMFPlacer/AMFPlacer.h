/**
 * @file AMFPlacer.h
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief The major file describing the overall workflow of AMFPlacer (an analytical mixed-size FPGA placer)
 * @version 0.1
 * @date 2021-06-03
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "DesignInfo.h"
#include "DeviceInfo.h"
#include "GlobalPlacer.h"
#include "IncrementalBELPacker.h"
#include "InitialPacker.h"
#include "ParallelCLBPacker.h"
#include "PlacementInfo.h"
#include "PlacementTimingOptimizer.h"
#include "utils/simpleJSON.h"
#include <boost/filesystem.hpp>
#include <iostream>
#include <omp.h>

/**
 * @brief AMFPlacer is an analytical mixed-size FPGA placer
 *
 * To enable the performance optimization of application mapping on modern field-programmable gate arrays (FPGAs),
 * certain critical path portions of the designs might be prearranged into many multi-cell macros during synthesis.
 * These movable macros with constraints of shape and resources lead to challenging mixed-size placement for FPGA
 * designs which cannot be addressed by previous works of analytical placers. In this work, we propose AMF-Placer,
 * an open-source analytical mixed-size FPGA placer supporting mixed-size placement on FPGA, with an interface to
 * Xilinx Vivado. To speed up the convergence and improve the quality of the placement, AMF-Placer is equipped with
 * a series of new techniques for wirelength optimization, cell spreading, packing, and legalization. Based on a set
 * of the latest large open-source benchmarks from various domains for Xilinx Ultrascale FPGAs, experimental results
 * indicate that AMF-Placer can improve HPWL by 20.4%-89.3% and reduce runtime by 8.0%-84.2%, compared to the
 * baseline. Furthermore, utilizing the parallelism of the proposed algorithms, with 8 threads, the placement procedure
 * can be accelerated by 2.41x on average.
 *
 */
class AMFPlacer
{
  public:
    /**
     * @brief Construct a new AMFPlacer object according to a given placer configuration file
     *
     * @param JSONFileName
     */
    AMFPlacer(std::string JSONFileName)
    {
        JSON = parseJSONFile(JSONFileName);

        assert(JSON.find("vivado extracted device information file") != JSON.end());
        assert(JSON.find("special pin offset info file") != JSON.end());
        assert(JSON.find("vivado extracted design information file") != JSON.end());
        assert(JSON.find("cellType2fixedAmo file") != JSON.end());
        assert(JSON.find("cellType2sharedCellType file") != JSON.end());
        assert(JSON.find("sharedCellType2BELtype file") != JSON.end());
        assert(JSON.find("GlobalPlacementIteration") != JSON.end());
        if (JSON.find("dumpDirectory") != JSON.end())
        {
            if (!fileExists(JSON["dumpDirectory"]))
                assert(boost::filesystem::create_directories(JSON["dumpDirectory"]) &&
                       "the specified dump directory should be created successfully.");
        }

        oriTime = std::chrono::steady_clock::now();

        omp_set_num_threads(std::stoi(JSON["jobs"]));
        if (JSON.find("jobs") != JSON.end())
        {
            omp_set_num_threads(std::stoi(JSON["jobs"]));
        }
        else
        {
            omp_set_num_threads(1);
        }

        // load device information
        deviceinfo = new DeviceInfo(JSON, "VCU108");
        deviceinfo->printStat();

        // load design information
        designInfo = new DesignInfo(JSON, deviceinfo);
        designInfo->printStat();
    };

    ~AMFPlacer()
    {
        delete placementInfo;
        delete designInfo;
        delete deviceinfo;
        if (incrementalBELPacker)
            delete incrementalBELPacker;
        if (globalPlacer)
            delete globalPlacer;
        if (initialPacker)
            delete initialPacker;
    }

    void clearSomeAttributesCannotRecord()
    {
        for (auto PU : placementInfo->getPlacementUnits())
        {
            if (PU->isPacked())
                PU->resetPacked();
        }
        for (auto pair : placementInfo->getPULegalXY().first)
        {
            if (pair.first->isFixed() && !pair.first->isLocked())
            {
                pair.first->setUnfixed();
            }
        }
    }

    /**
     * @brief launch the analytical mixed-size FPGA placement procedure
     *
     */
    void run()
    {
        // initialize placement information, including how to map cells to BELs
        placementInfo = new PlacementInfo(designInfo, deviceinfo, JSON);

        // we have to pack cells in design info into placement units in placement info with packer
        InitialPacker *initialPacker = new InitialPacker(designInfo, deviceinfo, placementInfo, JSON);
        initialPacker->pack();
        placementInfo->resetLUTFFDeterminedOccupation();

        placementInfo->printStat();
        placementInfo->createGridBins(5.0, 5.0);
        placementInfo->verifyDeviceForDesign();

        placementInfo->buildSimpleTimingGraph();
        PlacementTimingOptimizer *timingOptimizer = new PlacementTimingOptimizer(placementInfo, JSON);
        int longPathThr = placementInfo->getLongPathThresholdLevel();
        // int mediumPathThr = placementInfo->getMediumPathThresholdLevel();

        // go through several glable placement iterations to get initial placement
        globalPlacer = new GlobalPlacer(placementInfo, JSON);

        // enable the timing optimization, start initial placement and global placement.

        globalPlacer->clusterPlacement();
        timingOptimizer->clusterLongPathInOneClockRegion(longPathThr, 0.5);
        globalPlacer->GlobalPlacement_fixedCLB(1, 0.0002);

        globalPlacer->GlobalPlacement_CLBElements(std::stoi(JSON["GlobalPlacementIteration"]) / 3, false, 5, true, true,
                                                  200, timingOptimizer);
        timingOptimizer->clusterLongPathInOneClockRegion(longPathThr, 0.5);
        globalPlacer->setPseudoNetWeight(globalPlacer->getPseudoNetWeight() * 0.85);
        globalPlacer->setMacroLegalizationParameters(globalPlacer->getMacroPseudoNetEnhanceCnt() * 0.8,
                                                     globalPlacer->getMacroLegalizationWeight() * 0.8);
        placementInfo->createGridBins(2.0, 2.0);
        placementInfo->adjustLUTFFUtilization(-10, true);
        // globalPlacer->spreading(-1);
        globalPlacer->GlobalPlacement_CLBElements(std::stoi(JSON["GlobalPlacementIteration"]) * 2 / 9, true, 5, true,
                                                  true, 200, timingOptimizer);
        placementInfo->getPU2ClockRegionCenters().clear();
        print_info("Current Total HPWL = " + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));

        // pack simple LUT-FF pairs and go through several global placement iterations
        incrementalBELPacker = new IncrementalBELPacker(designInfo, deviceinfo, placementInfo, JSON);
        incrementalBELPacker->LUTFFPairing(4.0);
        incrementalBELPacker->FFPairing(4.0);
        placementInfo->printStat();
        print_info("Current Total HPWL = " + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));

        timingOptimizer->clusterLongPathInOneClockRegion(longPathThr, 0.5);

        globalPlacer->setPseudoNetWeight(globalPlacer->getPseudoNetWeight() * 0.85);
        globalPlacer->setMacroLegalizationParameters(globalPlacer->getMacroPseudoNetEnhanceCnt() * 0.8,
                                                     globalPlacer->getMacroLegalizationWeight() * 0.8);
        globalPlacer->setNeighborDisplacementUpperbound(3.0);

        globalPlacer->GlobalPlacement_CLBElements(std::stoi(JSON["GlobalPlacementIteration"]) * 2 / 9, true, 5, true,
                                                  true, 25, timingOptimizer);
        // placementInfo->getPU2ClockRegionCenters().clear();

        // placementInfo->getDesignInfo()->resetNetEnhanceRatio();
        // timingOptimizer->enhanceNetWeight_LevelBased(mediumPathThr);
        globalPlacer->setNeighborDisplacementUpperbound(2.0);

        // timingOptimizer->moveDriverIntoBetterClockRegion(longPathThr, 0.75);
        globalPlacer->GlobalPlacement_CLBElements(std::stoi(JSON["GlobalPlacementIteration"]) * 2 / 9, true, 5, true,
                                                  true, 25, timingOptimizer);
        // placementInfo->getPU2ClockRegionCenters().clear();
        globalPlacer->GlobalPlacement_CLBElements(std::stoi(JSON["GlobalPlacementIteration"]) / 2, true, 5, true, false,
                                                  25, timingOptimizer);

        // currently, some fixed/packed flag cannot be stored in the check-point (TODO)
        clearSomeAttributesCannotRecord();

        // test the check-point mechanism
        placementInfo->dumpPlacementUnitInformation(JSON["dumpDirectory"] + "/PUInfoBeforeFinalPacking");
        placementInfo->loadPlacementUnitInformation(JSON["dumpDirectory"] + "/PUInfoBeforeFinalPacking.gz");
        print_info("Current Total HPWL = " + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));

        timingOptimizer->conductStaticTimingAnalysis();
        // finally pack the elements into sites on the FPGA device
        parallelCLBPacker =
            new ParallelCLBPacker(designInfo, deviceinfo, placementInfo, JSON, 3, 10, 0.25, 0.5, 6, 10, 0.1, "first");
        parallelCLBPacker->packCLBs(30, true);
        parallelCLBPacker->setPULocationToPackedSite();
        timingOptimizer->conductStaticTimingAnalysis();
        placementInfo->checkClockUtilization(true);
        print_info("Current Total HPWL = " + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));
        placementInfo->resetLUTFFDeterminedOccupation();
        parallelCLBPacker->updatePackedMacro(true, true);
        placementInfo->adjustLUTFFUtilization(1, true);
        placementInfo->dumpCongestion(JSON["dumpDirectory"] + "/congestionInfo");

        if (parallelCLBPacker)
            delete parallelCLBPacker;

        // currently, some fixed/packed flag cannot be stored in the check-point (TODO)
        clearSomeAttributesCannotRecord();
        placementInfo->dumpPlacementUnitInformation(JSON["dumpDirectory"] + "/PUInfoFinal");
        placementInfo->checkClockUtilization(true);

        print_status("Placement Done");
        print_info("Current Total HPWL = " + std::to_string(placementInfo->updateB2BAndGetTotalHPWL()));

        // auto nowTime = std::chrono::steady_clock::now();
        // auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - oriTime).count();

        return;
    }

  private:
    /**
     * @brief information related to the device (BELs, Sites, Tiles, Clock Regions)
     *
     */
    DeviceInfo *deviceinfo = nullptr;

    /**
     * @brief information related to the design (cells, pins and nets)
     *
     */
    DesignInfo *designInfo = nullptr;

    /**
     * @brief inforamtion related to placement (locations, interconnections, status, constraints, legalization)
     *
     */
    PlacementInfo *placementInfo = nullptr;

    /**
     * @brief initially packing for macro extraction based on pre-defined rules
     *
     */
    InitialPacker *initialPacker = nullptr;

    /**
     * @brief incremental pairing of some FFs and LUTs into small macros
     *
     */
    IncrementalBELPacker *incrementalBELPacker = nullptr;

    /**
     * @brief global placer acconting for initial placement, quadratic placement, cell spreading and macro legalization.
     *
     */
    GlobalPlacer *globalPlacer = nullptr;

    /**
     * @brief final packing of instances into CLB sites
     *
     */
    ParallelCLBPacker *parallelCLBPacker = nullptr;

    /**
     * @brief the user-defined settings of placement
     *
     */
    std::map<std::string, std::string> JSON;
};
