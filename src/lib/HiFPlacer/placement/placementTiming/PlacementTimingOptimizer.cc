/**
 * @file PlacementTimingOptimizer.cc
 * @author Tingyuan LIANG (tliang@connect.ust.hk)
 * @brief
 * @version 0.1
 * @date 2021-10-02
 *
 * @copyright Copyright (c) 2021 Reconfiguration Computing Systems Lab, The Hong Kong University of Science and
 * Technology. All rights reserved.
 *
 */

#include "PlacementTimingOptimizer.h"

#include <cmath>
#include <codecvt>

PlacementTimingOptimizer::PlacementTimingOptimizer(PlacementInfo *placementInfo,
                                                   std::map<std::string, std::string> &JSONCfg)
    : placementInfo(placementInfo), timingInfo(placementInfo->getTimingInfo()), JSONCfg(JSONCfg)
{
    if (JSONCfg.find("PlacementTimingOptimizerVerbose") != JSONCfg.end())
        verbose = JSONCfg["PlacementTimingOptimizerVerbose"] == "true";
    if (JSONCfg.find("y2xRatio") != JSONCfg.end())
        y2xRatio = std::stof(JSONCfg["y2xRatio"]);

    designInfo = placementInfo->getDesignInfo();
    deviceInfo = placementInfo->getDeviceInfo();
    initPois();
}

void PlacementTimingOptimizer::enhanceNetWeight_LevelBased(int levelThr)
{
    bool printOut = false;
    std::string dumpFileName = "optNetInfo.txt";
    std::ofstream outfile0;
    if (JSONCfg.find("PlacementTimingOptimizer_EnhanceNetWeightLevelBasedLog") != JSONCfg.end())
    {
        printOut = true;
        std::string dumpFileName = JSONCfg["PlacementTimingOptimizer_EnhanceNetWeightLevelBasedLog"];
        print_status("PlacementTimingOptimizer: dumping enhanceNetWeight_LevelBased to: " + dumpFileName);
        outfile0.open(dumpFileName.c_str());
        assert(outfile0.is_open() && outfile0.good() &&
               "The path for PlacementTimingOptimizer_EnhanceNetWeightLevelBasedLog does not exist and please check "
               "your path settings");
    }

    print_status("PlacementTimingOptimizer: enhanceNetWeight_LevelBased starts.");
    if (levelThr < 4)
        return;
    assert(timingInfo);

    float maxEnhanceRatio = 0;
    auto timingNodes = timingInfo->getSimplePlacementTimingInfo();
    for (auto tmpNet : designInfo->getNets())
        tmpNet->setOverallTimingNetEnhancement(1.0);

    enhanceNetWeight_LevelBased_Cnt++;
    effectFactor = (enhanceNetWeight_LevelBased_Cnt / 30.0);
    if (effectFactor < 1)
        effectFactor = std::pow(effectFactor, 1);
    else
        effectFactor = 1;

    for (auto cellA : designInfo->getCells())
    {
        if (cellA->isVirtualCell())
            continue;
        int targetPathLen = timingNodes[cellA->getCellId()]->getLongestPathLength();
        if (targetPathLen < levelThr)
            continue;

        // int pinAIdInNet = 0;
        for (DesignInfo::DesignPin *curPinA : cellA->getPins())
        {
            if (curPinA->getNet())
            {
                if (curPinA->isInputPort())
                    continue;
                // int pinBIdInNet = 0;
                auto pins = curPinA->getNet()->getPins();
                int pinNum = pins.size();
                if (pinNum <= 1 || pinNum >= 1000)
                    continue;

                float enhanceRatio;
                float overflowRatio = std::pow((float)0.8 * targetPathLen / levelThr, 1);
                // if (overflowRatio > 10)
                //     overflowRatio = 10;
                if (pinNum < 200)
                    enhanceRatio = 1.5 * (overflowRatio + 0.005 * pinNum);
                else
                    enhanceRatio = 1.5 * (overflowRatio + 1);

                enhanceRatio = std::pow(enhanceRatio, effectFactor);
                if (enhanceRatio > maxEnhanceRatio)
                    maxEnhanceRatio = enhanceRatio;
                curPinA->getNet()->enhanceOverallTimingNetEnhancement(enhanceRatio);

                if (printOut)
                {
                    outfile0 << "enhanced net: [ " << curPinA->getName()
                             << "] fanOut:" << curPinA->getNet()->getPins().size() << " by " << enhanceRatio << "\n";
                }
            }
        }
    }
    if (printOut)
    {
        for (auto cellA : designInfo->getCells())
        {
            if (cellA->isVirtualCell())
                continue;
            int targetPathLen = timingNodes[cellA->getCellId()]->getLongestPathLength();

            outfile0 << "cell path len: [ " << cellA->getName() << "] = " << targetPathLen << "\n";
        }
        outfile0.close();
    }

    print_status("PlacementTimingOptimizer: enhanceNetWeight_LevelBased done (maxEnhancedRatio=" +
                 std::to_string(maxEnhanceRatio) + ")");
}

void PlacementTimingOptimizer::setPinsLocation()
{
    auto &cellLoc = placementInfo->getCellId2location();
    auto &pinLoc = placementInfo->getPinId2location();
    pinLoc = std::vector<PlacementInfo::Location>(designInfo->getPins().size());
    for (auto tmpCell : placementInfo->getCells())
    {
        int cellId = tmpCell->getCellId();
        auto tmpCellLoc = cellLoc[cellId];
        for (auto tmpPin : tmpCell->getPins())
        {
            float pinX = tmpCellLoc.X + tmpPin->getOffsetXInCell();
            float pinY = tmpCellLoc.Y + tmpPin->getOffsetYInCell();
            assert((unsigned int)tmpPin->getElementIdInType() < pinLoc.size());
            pinLoc[tmpPin->getElementIdInType()].X = pinX;
            pinLoc[tmpPin->getElementIdInType()].Y = pinY;
        }
    }
}

void PlacementTimingOptimizer::conductStaticTimingAnalysis()
{

    bool printOut = false;
    std::string dumpFileName = "optNetDelayInfo.txt";
    std::ofstream outfile0;
    if (JSONCfg.find("PlacementTimingOptimizer_EdgesDelayLog") != JSONCfg.end())
    {
        printOut = true;
        std::string dumpFileName = JSONCfg["PlacementTimingOptimizer_EdgesDelayLog"];
        print_status("PlacementTimingOptimizer: dumping PlacementTimingOptimizer_EdgesDelayLog to: " + dumpFileName);
        outfile0.open(dumpFileName.c_str());
        assert(outfile0.is_open() && outfile0.good() &&
               "The path for PlacementTimingOptimizer_EdgesDelayLog does not exist and please check "
               "your path settings");
    }

    assert(timingInfo);
    auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
    setPinsLocation();

    auto &pinLoc = placementInfo->getPinId2location();

    for (auto edge : timingGraph->getEdges())
    {
        auto &pin1Loc = pinLoc[edge->getSourcePin()->getElementIdInType()];
        auto &pin2Loc = pinLoc[edge->getSinkPin()->getElementIdInType()];
        if (pin1Loc.X < -5 && pin1Loc.Y < -5)
            continue;
        if (pin2Loc.X < -5 && pin2Loc.Y < -5)
            continue;

        int clockRegionX0, clockRegionY0;
        deviceInfo->getClockRegionByLocation(pin1Loc.X, pin1Loc.Y, clockRegionX0, clockRegionY0);
        int clockRegionX1, clockRegionY1;
        deviceInfo->getClockRegionByLocation(pin2Loc.X, pin2Loc.Y, clockRegionX1, clockRegionY1);
        edge->setDelay(getDelayByModel(std::fabs(pin1Loc.X - pin2Loc.X), std::fabs(pin1Loc.Y - pin2Loc.Y)) +
                       std::abs(clockRegionX1 - clockRegionX0) * 0.5);
    }

    timingGraph->propogateArrivalTime();
    timingGraph->backPropogateRequiredArrivalTime();
    float maxDelay = 0;
    int maxDelayId = -1;
    for (unsigned int i = 0; i < timingGraph->getNodes().size(); i++)
    {
        if (timingGraph->getNodes()[i]->getLatestArrival() > maxDelay)
        {
            maxDelay = timingGraph->getNodes()[i]->getLatestArrival();
            maxDelayId = i;
        }
    }

    auto resPath = timingGraph->backTraceDelayLongestPathFromNode(maxDelayId);

    std::cout << "An example of long delay path for the current placement:\n";
    for (auto id : resPath)
    {
        std::cout << designInfo->getCells()[id]->getName()
                  << "   [delay]: " << timingGraph->getNodes()[id]->getLatestArrival()
                  << "   [required]: " << timingGraph->getNodes()[id]->getRequiredArrivalTime() << "\n";
    }

    if (printOut)
    {
        for (auto node : timingInfo->getSimplePlacementTimingInfo())
        {
            if (node->getOutEdges().size() > 32)
                continue;
            for (auto edge : node->getOutEdges())
            {
                outfile0 << " src:" << edge->getSourcePin()->getName() << " sink:" << edge->getSinkPin()->getName()
                         << " delay:" << edge->getDelay() << "\n";
            }
        }
        outfile0.close();
    }
}

void PlacementTimingOptimizer::clusterLongPathInOneClockRegion(int pathLenThr, float clusterThrRatio)
{
    print_warning("PlacementTimingOptimizer: clustering long path in one clock region");

    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo_PathLenSorted();
    auto simpleTimingGraph = placementInfo->getTimingInfo()->getSimplePlacementTimingGraph();
    auto &cellLoc = placementInfo->getCellId2location();
    auto deviceInfo = placementInfo->getDeviceInfo();
    auto YX2ClockRegion = deviceInfo->getClockRegions();
    auto &PU2ClockRegionCenter = placementInfo->getPU2ClockRegionCenters();
    PU2ClockRegionCenter.clear();

    std::set<int> extractedCellIds;
    std::set<PlacementInfo::PlacementUnit *> extractedPUs;
    extractedCellIds.clear();
    extractedPUs.clear();
    clockRegionclusters.clear();

    unsigned int maxSize = 0;
    for (unsigned int nodeId = 0; nodeId < timingNodes.size() * 0.1; nodeId++)
    {
        auto timingNode = timingNodes[nodeId];
        if (timingNode->getLongestPathLength() > pathLenThr)
        {
            if (extractedCellIds.find(nodeId) != extractedCellIds.end())
                continue;
            auto candidateCellIds =
                simpleTimingGraph->DFSFromNode(timingNode->getId(), pathLenThr, 2000, extractedCellIds);

            if (candidateCellIds.size() >= pathLenThr * 0.8)
            {
                std::set<PlacementInfo::PlacementUnit *> PUsInLongPaths;
                PUsInLongPaths.clear();
                for (auto &candidateCellId : candidateCellIds)
                {
                    auto PUInPath = placementInfo->getPlacementUnitByCellId(candidateCellId);
                    if (extractedPUs.find(PUInPath) == extractedPUs.end() &&
                        PUsInLongPaths.find(PUInPath) == PUsInLongPaths.end())
                    {
                        PUsInLongPaths.insert(PUInPath);
                    }
                }

                if (PUsInLongPaths.size() >= 8)
                {
                    std::map<std::pair<int, int>, float> clockRegionYX2Cnt;
                    clockRegionYX2Cnt.clear();

                    float maxClockRegionWeight = 0;
                    float totalClockRegionWeight = 0;
                    std::pair<int, int> optClockLocYX;

                    for (auto tmpPU : PUsInLongPaths)
                    {
                        if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
                        {
                            int cellId = unpackedCell->getCell()->getCellId();
                            int clockRegionX, clockRegionY;
                            auto tmpLoc = cellLoc[cellId];
                            deviceInfo->getClockRegionByLocation(tmpLoc.X, tmpLoc.Y, clockRegionX, clockRegionY);
                            std::pair<int, int> tmpClockLocYX(-1, clockRegionX);
                            if (clockRegionYX2Cnt.find(tmpClockLocYX) == clockRegionYX2Cnt.end())
                            {
                                clockRegionYX2Cnt[tmpClockLocYX] = 0;
                            }
                            clockRegionYX2Cnt[tmpClockLocYX] += 1;
                            totalClockRegionWeight += 1;

                            if (clockRegionYX2Cnt[tmpClockLocYX] > maxClockRegionWeight)
                            {
                                maxClockRegionWeight = clockRegionYX2Cnt[tmpClockLocYX];
                                optClockLocYX = tmpClockLocYX;
                            }
                        }
                        else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
                        {
                            for (auto tmpCell : curMacro->getCells())
                            {
                                int cellId = tmpCell->getCellId();
                                int clockRegionX, clockRegionY;
                                auto tmpLoc = cellLoc[cellId];
                                deviceInfo->getClockRegionByLocation(tmpLoc.X, tmpLoc.Y, clockRegionX, clockRegionY);

                                std::pair<int, int> tmpClockLocYX(-1, clockRegionX);
                                if (clockRegionYX2Cnt.find(tmpClockLocYX) == clockRegionYX2Cnt.end())
                                {
                                    clockRegionYX2Cnt[tmpClockLocYX] = 0;
                                }
                                clockRegionYX2Cnt[tmpClockLocYX] += 1;
                                totalClockRegionWeight += 1;

                                if (clockRegionYX2Cnt[tmpClockLocYX] > maxClockRegionWeight)
                                {
                                    maxClockRegionWeight = clockRegionYX2Cnt[tmpClockLocYX];
                                    optClockLocYX = tmpClockLocYX;
                                }
                            }
                        }
                    }

                    if ((maxClockRegionWeight > totalClockRegionWeight * clusterThrRatio) && maxClockRegionWeight >= 4)
                    {
                        auto optClockRegion = YX2ClockRegion[0][optClockLocYX.second];
                        float cX = (optClockRegion->getLeft() + optClockRegion->getRight()) / 2;
                        std::vector<int> PUIdsInLongPaths;
                        PUIdsInLongPaths.clear();
                        for (auto curPU : PUsInLongPaths)
                        {
                            if (!curPU->isFixed())
                            {
                                float fX = cX;
                                float fY = curPU->Y();
                                placementInfo->legalizeXYInArea(curPU, fX, fY);
                                // curPU->setAnchorLocationAndForgetTheOriginalOne(fX, fY);
                                extractedPUs.insert(curPU);

                                PU2ClockRegionCenter[curPU] = std::pair<float, float>(fX, fY);

                                if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(curPU))
                                {
                                    int cellId = unpackedCell->getCell()->getCellId();
                                    extractedCellIds.insert(cellId);
                                }
                                else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(curPU))
                                {
                                    for (auto tmpCell : curMacro->getCells())
                                    {
                                        int cellId = tmpCell->getCellId();
                                        extractedCellIds.insert(cellId);
                                    }
                                }
                                PUIdsInLongPaths.push_back(curPU->getId());
                            }
                        }

                        clockRegionclusters.push_back(PUIdsInLongPaths);
                        std::cout << "maxClockRegionWeight: " << maxClockRegionWeight
                                  << " totalClockRegionWeight:" << totalClockRegionWeight
                                  << " #extractedCellIds=" << extractedCellIds.size()
                                  << " #extractedPUs=" << extractedPUs.size()
                                  << " pathLength=" << timingNode->getLongestPathLength() << "\n";
                    }

                    else if (totalClockRegionWeight >= 20000)
                    {
                        for (auto tmpCellId : candidateCellIds)
                        {
                            extractedCellIds.insert(tmpCellId);
                        }
                    }
                }
            }
        }
        else
        {
            break;
        }
    }
    dumpClockRegionClusters();
    print_info("ClusterPlacer: largest long-path cluster size=" + std::to_string(maxSize));
}

void PlacementTimingOptimizer::dumpClockRegionClusters()
{
    std::string dumpClockRegionClustersFile = JSONCfg["Dump Cluster file"] + "-clockRegion";
    if (dumpClockRegionClustersFile != "")
    {
        print_status("dumping cluster information to " + dumpClockRegionClustersFile);
        std::ofstream outfile0((dumpClockRegionClustersFile + ".tcl").c_str());
        assert(outfile0.is_open() && outfile0.good() &&
               "The path for cluster result dumping does not exist and please check your path settings");
        for (unsigned int cluster_id = 0; cluster_id < clockRegionclusters.size(); cluster_id++)
        {
            outfile0 << "highlight -color_index " << (cluster_id) % 20 + 1 << "  [get_cells {";
            for (int id : clockRegionclusters[cluster_id])
            {
                if (auto tmpMacro =
                        dynamic_cast<PlacementInfo::PlacementMacro *>(placementInfo->getPlacementUnits()[id]))
                {
                    for (auto cell : tmpMacro->getCells())
                    {
                        outfile0 << cell->getName() << " ";
                    }
                }
                else if (auto tmpUnpacked = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(
                             placementInfo->getPlacementUnits()[id]))
                {
                    outfile0 << tmpUnpacked->getName() << " ";
                }
                else
                {
                    assert(false);
                }
            }
            outfile0 << "}]\n";
        }
        outfile0.close();
    }
}

void PlacementTimingOptimizer::moveDriverIntoBetterClockRegion(int pathLenThr, float clusterThrRatio)
{
    print_warning("PlacementTimingOptimizer: clustering long path in one clock region");

    auto &timingNodes = placementInfo->getTimingInfo()->getSimplePlacementTimingInfo_PathLenSorted();
    // auto simpleTimingGraph = placementInfo->getTimingInfo()->getSimplePlacementTimingGraph();
    auto &cellLoc = placementInfo->getCellId2location();
    auto deviceInfo = placementInfo->getDeviceInfo();
    auto YX2ClockRegion = deviceInfo->getClockRegions();
    auto &PU2ClockRegionCenter = placementInfo->getPU2ClockRegionCenters();
    PU2ClockRegionCenter.clear();

    std::set<int> extractedCellIds;
    std::set<PlacementInfo::PlacementUnit *> extractedPUs;
    extractedCellIds.clear();
    extractedPUs.clear();

    unsigned int maxSize = 0;
    for (unsigned int nodeId = 0; nodeId < timingNodes.size() * 0.1; nodeId++)
    {
        auto timingNode = timingNodes[nodeId];
        if (timingNode->getLongestPathLength() > pathLenThr)
        {
            if (extractedCellIds.find(nodeId) != extractedCellIds.end())
                continue;
            std::set<int> candidateCellIds;
            candidateCellIds.clear();
            auto driverPU = placementInfo->getPlacementUnitByCellId(nodeId);

            for (auto outEdge : timingNode->getOutEdges())
            {
                candidateCellIds.insert(outEdge->getSink()->getId());
            }

            if (candidateCellIds.size() >= pathLenThr * 0.5)
            {
                std::set<PlacementInfo::PlacementUnit *> PUsInLongPaths;
                PUsInLongPaths.clear();
                for (auto &candidateCellId : candidateCellIds)
                {
                    auto PUInPath = placementInfo->getPlacementUnitByCellId(candidateCellId);
                    PUsInLongPaths.insert(PUInPath);
                }

                if (PUsInLongPaths.size() >= 8)
                {
                    std::map<std::pair<int, int>, float> clockRegionYX2Cnt;
                    clockRegionYX2Cnt.clear();

                    float maxClockRegionWeight = 0;
                    float totalClockRegionWeight = 0;
                    std::pair<int, int> optClockLocYX;

                    auto driverLoc = cellLoc[nodeId];
                    int driverClockRegionX, driverClockRegionY;
                    deviceInfo->getClockRegionByLocation(driverLoc.X, driverLoc.Y, driverClockRegionX,
                                                         driverClockRegionY);
                    std::pair<int, int> driverClockLocYX(-1, driverClockRegionX);

                    for (auto tmpPU : PUsInLongPaths)
                    {
                        if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(tmpPU))
                        {
                            int cellId = unpackedCell->getCell()->getCellId();
                            int clockRegionX, clockRegionY;
                            auto tmpLoc = cellLoc[cellId];
                            deviceInfo->getClockRegionByLocation(tmpLoc.X, tmpLoc.Y, clockRegionX, clockRegionY);
                            std::pair<int, int> tmpClockLocYX(-1, clockRegionX);
                            if (clockRegionYX2Cnt.find(tmpClockLocYX) == clockRegionYX2Cnt.end())
                            {
                                clockRegionYX2Cnt[tmpClockLocYX] = 0;
                            }
                            clockRegionYX2Cnt[tmpClockLocYX] += 1;
                            totalClockRegionWeight += 1;

                            if (clockRegionYX2Cnt[tmpClockLocYX] > maxClockRegionWeight)
                            {
                                maxClockRegionWeight = clockRegionYX2Cnt[tmpClockLocYX];
                                optClockLocYX = tmpClockLocYX;
                            }
                        }
                        else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(tmpPU))
                        {
                            for (auto tmpCell : curMacro->getCells())
                            {
                                int cellId = tmpCell->getCellId();
                                int clockRegionX, clockRegionY;
                                auto tmpLoc = cellLoc[cellId];
                                deviceInfo->getClockRegionByLocation(tmpLoc.X, tmpLoc.Y, clockRegionX, clockRegionY);

                                std::pair<int, int> tmpClockLocYX(-1, clockRegionX);
                                if (clockRegionYX2Cnt.find(tmpClockLocYX) == clockRegionYX2Cnt.end())
                                {
                                    clockRegionYX2Cnt[tmpClockLocYX] = 0;
                                }
                                clockRegionYX2Cnt[tmpClockLocYX] += 1;
                                totalClockRegionWeight += 1;

                                if (clockRegionYX2Cnt[tmpClockLocYX] > maxClockRegionWeight)
                                {
                                    maxClockRegionWeight = clockRegionYX2Cnt[tmpClockLocYX];
                                    optClockLocYX = tmpClockLocYX;
                                }
                            }
                        }
                    }

                    if (driverClockLocYX != optClockLocYX &&
                        (maxClockRegionWeight > totalClockRegionWeight * clusterThrRatio) && maxClockRegionWeight >= 4)
                    {
                        auto optClockRegion = YX2ClockRegion[0][optClockLocYX.second];
                        float cX = (optClockRegion->getLeft() + optClockRegion->getRight()) / 2;

                        if (!driverPU->isFixed())
                        {
                            float fX = cX;
                            float fY = driverPU->Y();
                            placementInfo->legalizeXYInArea(driverPU, fX, fY);
                            // curPU->setAnchorLocationAndForgetTheOriginalOne(fX, fY);
                            extractedPUs.insert(driverPU);

                            PU2ClockRegionCenter[driverPU] = std::pair<float, float>(fX, fY);

                            if (auto unpackedCell = dynamic_cast<PlacementInfo::PlacementUnpackedCell *>(driverPU))
                            {
                                int cellId = unpackedCell->getCell()->getCellId();
                                extractedCellIds.insert(cellId);
                            }
                            else if (auto curMacro = dynamic_cast<PlacementInfo::PlacementMacro *>(driverPU))
                            {
                                for (auto tmpCell : curMacro->getCells())
                                {
                                    int cellId = tmpCell->getCellId();
                                    extractedCellIds.insert(cellId);
                                }
                            }
                        }

                        std::cout << "maxClockRegionWeight: " << maxClockRegionWeight
                                  << " totalClockRegionWeight:" << totalClockRegionWeight
                                  << " #extractedCellIds=" << extractedCellIds.size()
                                  << " #extractedPUs=" << extractedPUs.size()
                                  << " pathLength=" << timingNode->getLongestPathLength() << "\n";
                    }

                    else if (totalClockRegionWeight >= 20000)
                    {
                        for (auto tmpCellId : candidateCellIds)
                        {
                            extractedCellIds.insert(tmpCellId);
                        }
                    }
                }
            }
        }
        else
        {
            break;
        }
    }
    print_info("ClusterPlacer: largest long-path cluster size=" + std::to_string(maxSize));
}

void PlacementTimingOptimizer::propogateArrivalTime()
{
    assert(timingInfo);
    // auto timingGraph = timingInfo->getSimplePlacementTimingGraph();
}