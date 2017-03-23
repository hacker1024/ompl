/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2014, Rice University
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Rice University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Author: Zachary Kingston */

#include "ConstrainedPlanningCommon.h"

/** Print usage information. Does not return. */
void usage(const char *const progname)
{
    std::cout << "Usage: " << progname << " <problem> <planner> <timelimit> <-c?>\n";
    printProblems();
    printPlanners();
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc != 5 && argc != 6)
        usage(argv[0]);

    double sleep = 0;

    // Detect artifical validity checking delay.
    int verbose = 0;
    if (argc == 6)
    {
        if (strcmp(argv[5], "-v") != 0)
            usage(argv[0]);
        verbose = 1;
    }

    int mode = 0;
    if (strcmp("-a", argv[4]) == 0)
        mode = 1;
    else if (strcmp("-p", argv[4]) == 0)
        mode = 2;

    // Initialize the atlas for the problem's manifold

    Eigen::VectorXd x, y;
    ompl::base::StateValidityCheckerFn isValid;
    ompl::base::Constraint *constraint = parseProblem(argv[1], x, y, isValid, sleep);

    if (mode == 1)
    {
        ompl::base::AtlasStateSpacePtr atlas(new ompl::base::AtlasStateSpace(constraint->getAmbientSpace(), constraint));
        if (!atlas)
            usage(argv[0]);

        // All the 'Constrained' classes are loose wrappers for the normal
        // classes. No effect except on the two special planners.
        ompl::geometric::SimpleSetup ss(atlas);
        ompl::base::SpaceInformationPtr si = ss.getSpaceInformation();
        atlas->setSpaceInformation(si);
        ss.setStateValidityChecker(isValid);
        si->setValidStateSamplerAllocator(avssa);

        // Atlas parameters
        atlas->setExploration(0.5);
        atlas->setRho(0.5);  // 0.2
        atlas->setAlpha(M_PI / 8);
        atlas->setEpsilon(0.2);  // 0.1
        atlas->setDelta(0.02);
        atlas->setMaxChartsPerExtension(200);

        // The atlas needs some place to start sampling from. We will make start and goal charts.
        ompl::base::AtlasChart *startChart = atlas->anchorChart(x);
        ompl::base::AtlasChart *goalChart = atlas->anchorChart(y);
        ompl::base::ScopedState<> start(atlas);
        ompl::base::ScopedState<> goal(atlas);
        start->as<ompl::base::AtlasStateSpace::StateType>()->setRealState(x, startChart);
        goal->as<ompl::base::AtlasStateSpace::StateType>()->setRealState(y, goalChart);
        ss.setStartAndGoalStates(start, goal);

        // Bounds
        ompl::base::RealVectorBounds bounds(atlas->getAmbientDimension());
        bounds.setLow(-10);
        bounds.setHigh(10);
        atlas->as<ompl::base::RealVectorStateSpace>()->setBounds(bounds);

        // Choose the planner.
        ompl::base::PlannerPtr planner(parsePlanner(argv[2], si, atlas->getRho_s()));
        if (!planner)
            usage(argv[0]);
        ss.setPlanner(planner);
        ss.setup();

        // Set the time limit
        const double runtime_limit = std::atof(argv[3]);
        if (runtime_limit <= 0)
            usage(argv[0]);

        // Plan. For 3D problems, we save the chart mesh, planner graph, and
        // solution path in the .ply format. Regardless of dimension, we write
        // the doubles in the path states to a .txt file.
        std::clock_t tstart = std::clock();
        ompl::base::PlannerStatus stat = planner->solve(runtime_limit);
        if (stat)
        {
            const double time = ((double)(std::clock() - tstart)) / CLOCKS_PER_SEC;

            ompl::geometric::PathGeometric &path = ss.getSolutionPath();
            if (x.size() == 3 && verbose)
            {
                std::ofstream pathFile("path.ply");
                atlas->dumpPath(path, pathFile, false);
                pathFile.close();
            }

            // Extract the full solution path by re-interpolating between the
            // saved states (except for the special planners)
            const std::vector<ompl::base::State *> &waypoints = path.getStates();
            double length = 0;

            std::ofstream animFile("anim.txt");
            for (std::size_t i = 0; i < waypoints.size() - 1; i++)
            {
                // Denote that we are switching to the next saved state
                // std::cout << "-----\n";
                ompl::base::AtlasStateSpace::StateType *from, *to;
                from = waypoints[i]->as<ompl::base::AtlasStateSpace::StateType>();
                to = waypoints[i + 1]->as<ompl::base::AtlasStateSpace::StateType>();

                // Traverse the manifold
                std::vector<ompl::base::State *> stateList;
                atlas->traverseManifold(from, to, true, &stateList);
                if (atlas->equalStates(stateList.front(), stateList.back()))
                {
                    // std::cout << "[" << stateList.front()->constVectorView().transpose() << "]  " <<
                    // stateList.front()->getChart()->getID() << "\n";
                    animFile << stateList.front()->as<ompl::base::AtlasStateSpace::StateType>()->constVectorView().transpose() << "\n";
                }
                else
                {
                    // Print the intermediate states
                    for (std::size_t i = 1; i < stateList.size(); i++)
                    {
                        // std::cout << "[" << stateList[i]->constVectorView().transpose() << "]  " <<
                        // stateList[i]->getChart()->getID() << "\n";
                        animFile << stateList[i]->as<ompl::base::AtlasStateSpace::StateType>()->constVectorView().transpose() << "\n";
                        length += atlas->distance(stateList[i - 1], stateList[i]);
                    }
                }

                // Delete the intermediate states
                for (auto &state : stateList)
                    atlas->freeState(state);
            }
            animFile.close();

            if (stat == ompl::base::PlannerStatus::APPROXIMATE_SOLUTION)
                std::cout << "Solution is approximate.\n";
            std::cout << "Length: " << length << "\n";
            std::cout << "Took " << time << " seconds.\n";
        }
        else
        {
            std::cout << "No solution found.\n";
        }

        ompl::base::PlannerData data(si);
        planner->getPlannerData(data);
        if (data.properties.find("approx goal distance REAL") != data.properties.end())
            std::cout << "Approx goal distance: "
                     << data.properties["approx goal distance REAL"] << "\n";

        std::cout << "Atlas created " << atlas->getChartCount() << " charts.\n";

        if (x.size() == 3 && verbose)
        {
            std::ofstream atlasFile("atlas.ply");
            atlas->dumpMesh(atlasFile);
            atlasFile.close();

            std::ofstream graphFile("graph.ply");
            ompl::base::PlannerData pd(si);
            planner->getPlannerData(pd);
            atlas->dumpGraph(pd.toBoostGraph(), graphFile, false);
            graphFile.close();

            std::cout << atlas->estimateFrontierPercent() << "% open.\n";
        }
    }

    else if (mode == 2)
    {
        ompl::base::ProjectedStateSpacePtr proj(
            new ompl::base::ProjectedStateSpace(constraint->getAmbientSpace(), constraint));
        if (!proj)
            usage(argv[0]);

        // All the 'Constrained' classes are loose wrappers for the normal
        // classes. No effect except on the two special planners.
        ompl::geometric::SimpleSetup ss(proj);
        ompl::base::SpaceInformationPtr si = ss.getSpaceInformation();
        proj->setSpaceInformation(si);
        ss.setStateValidityChecker(isValid);
        si->setValidStateSamplerAllocator(pvssa);

        proj->setDelta(0.02);

        // The proj needs some place to start sampling from. We will make start
        // and goal charts.
        ompl::base::ScopedState<> start(proj);
        ompl::base::ScopedState<> goal(proj);
        start->as<ompl::base::ProjectedStateSpace::StateType>()->setRealState(x);
        goal->as<ompl::base::ProjectedStateSpace::StateType>()->setRealState(y);
        ss.setStartAndGoalStates(start, goal);

        // Bounds
        ompl::base::RealVectorBounds bounds(proj->getAmbientDimension());
        bounds.setLow(-10);
        bounds.setHigh(10);
        proj->as<ompl::base::RealVectorStateSpace>()->setBounds(bounds);

        // Choose the planner.
        ompl::base::PlannerPtr planner(parsePlanner(argv[2], si, 0.7));
        if (!planner)
            usage(argv[0]);
        ss.setPlanner(planner);
        ss.setup();

        // Set the time limit
        const double runtime_limit = std::atof(argv[3]);
        if (runtime_limit <= 0)
            usage(argv[0]);

        // Plan. For 3D problems, we save the chart mesh, planner graph, and
        // solution path in the .ply format. Regardless of dimension, we write
        // the doubles in the path states to a .txt file.
        std::clock_t tstart = std::clock();
        ompl::base::PlannerStatus stat = planner->solve(runtime_limit);
        if (stat)
        {
            const double time = ((double)(std::clock() - tstart)) / CLOCKS_PER_SEC;

            ompl::geometric::PathGeometric &path = ss.getSolutionPath();
            if (x.size() == 3 && verbose)
            {
                std::ofstream pathFile("path.ply");
                proj->dumpPath(path, pathFile, false);
                pathFile.close();
            }

            // Extract the full solution path by re-interpolating between the
            // saved states (except for the special planners)
            const std::vector<ompl::base::State *> &waypoints = path.getStates();
            double length = 0;

            std::ofstream animFile("anim.txt");
            for (std::size_t i = 0; i < waypoints.size() - 1; i++)
            {
                // Denote that we are switching to the next saved state
                // std::cout << "-----\n";
                ompl::base::ProjectedStateSpace::StateType *from, *to;
                from = waypoints[i]->as<ompl::base::ProjectedStateSpace::StateType>();
                to = waypoints[i + 1]->as<ompl::base::ProjectedStateSpace::StateType>();

                // Traverse the manifold
                std::vector<ompl::base::State *> stateList;
                proj->traverseManifold(from, to, true, &stateList);
                if (proj->equalStates(stateList.front(), stateList.back()))
                    animFile << stateList.front()->as<ompl::base::ProjectedStateSpace::StateType>()->constVectorView().transpose() << "\n";
                else
                {
                    // Print the intermediate states
                    for (std::size_t i = 1; i < stateList.size(); i++)
                    {
                        animFile << stateList[i]->as<ompl::base::ProjectedStateSpace::StateType>()->constVectorView().transpose() << "\n";
                        length += proj->distance(stateList[i - 1], stateList[i]);
                    }
                }

                // Delete the intermediate states
                for (auto &state : stateList)
                    proj->freeState(state);
            }
            animFile.close();

            if (stat == ompl::base::PlannerStatus::APPROXIMATE_SOLUTION)
                std::cout << "Solution is approximate.\n";
            std::cout << "Length: " << length << "\n";
            std::cout << "Took " << time << " seconds.\n";
        }
        else
        {
            std::cout << "No solution found.\n";
        }

        ompl::base::PlannerData data(si);
        planner->getPlannerData(data);
        if (data.properties.find("approx goal distance REAL") != data.properties.end())
            std::cout << "Approx goal distance: "
                     << data.properties["approx goal distance REAL"] << "\n";

        if (x.size() == 3 && verbose)
        {
            std::ofstream graphFile("graph.ply");
            ompl::base::PlannerData pd(si);
            planner->getPlannerData(pd);
            proj->dumpGraph(pd.toBoostGraph(), graphFile, false);
            graphFile.close();
        }
    }

    return 0;
}
