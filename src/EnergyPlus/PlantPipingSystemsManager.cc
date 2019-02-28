// EnergyPlus, Copyright (c) 1996-2019, The Board of Trustees of the University of Illinois,
// The Regents of the University of California, through Lawrence Berkeley National Laboratory
// (subject to receipt of any required approvals from the U.S. Dept. of Energy), Oak Ridge
// National Laboratory, managed by UT-Battelle, Alliance for Sustainable Energy, LLC, and other
// contributors. All rights reserved.
//
// NOTICE: This Software was developed under funding from the U.S. Department of Energy and the
// U.S. Government consequently retains certain rights. As such, the U.S. Government has been
// granted for itself and others acting on its behalf a paid-up, nonexclusive, irrevocable,
// worldwide license in the Software to reproduce, distribute copies to the public, prepare
// derivative works, and perform publicly and display publicly, and to permit others to do so.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this list of
//     conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice, this list of
//     conditions and the following disclaimer in the documentation and/or other materials
//     provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National Laboratory,
//     the University of Illinois, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without specific prior
//     written permission.
//
// (4) Use of EnergyPlus(TM) Name. If Licensee (i) distributes the software in stand-alone form
//     without changes from the version obtained under this License, or (ii) Licensee makes a
//     reference solely to the software portion of its product, Licensee must refer to the
//     software as "EnergyPlus version X" software, where "X" is the version number Licensee
//     obtained under this License and may not use a different name for the software. Except as
//     specifically required in this Section (4), Licensee shall not use in a company name, a
//     product name, in advertising, publicity, or other promotional activities any name, trade
//     name, trademark, logo, or other designation of "EnergyPlus", "E+", "e+" or confusingly
//     similar designation, without the U.S. Department of Energy's prior written consent.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// C++ Headers
#include <cassert>
#include <cmath>
#include <fstream>
#include <memory>
#include <set>
#include <utility>

// ObjexxFCL Headers
#include <ObjexxFCL/Array.functions.hh>
#include <ObjexxFCL/Fmath.hh>
#include <ObjexxFCL/floops.hh>
#include <ObjexxFCL/gio.hh>
#include <ObjexxFCL/string.functions.hh>

// EnergyPlus Headers
#include <BranchNodeConnections.hh>
#include <DataEnvironment.hh>
#include <DataGlobals.hh>
#include <DataHVACGlobals.hh>
#include <DataHeatBalFanSys.hh>
#include <DataHeatBalSurface.hh>
#include <DataHeatBalance.hh>
#include <DataIPShortCuts.hh>
#include <DataLoopNode.hh>
#include <DataPlant.hh>
#include <DataPrecisionGlobals.hh>
#include <DataSurfaces.hh>
#include <FluidProperties.hh>
#include <General.hh>
#include <GlobalNames.hh>
#include <GroundTemperatureModeling/GroundTemperatureModelManager.hh>
#include <InputProcessing/InputProcessor.hh>
#include <NodeInputManager.hh>
#include <OutputProcessor.hh>
#include <PlantPipingSystemsManager.hh>
#include <PlantUtilities.hh>
#include <UtilityRoutines.hh>

namespace EnergyPlus {

    namespace PlantPipingSystemsManager {

        // Module containing the routines dealing with the PipingSystems

        // MODULE INFORMATION:
        //       AUTHOR         Edwin Lee
        //       DATE WRITTEN   Summer 2011
        //       MODIFIED       na
        //       RE-ENGINEERED  na

        // PURPOSE OF THIS MODULE:
        // Simulate all cases of plant "piping systems"
        //      PipingSystem:Underground
        //      PipingSystem:Generalized

        // METHODOLOGY EMPLOYED:
        // A 3D mesh is established, with full 3D conduction being employed
        // For ground simulation, moisture content and soil freezing is included
        // The mesh can include any number of pipe circuits placed within the domain
        // The mesh can interact with basement walls also

#pragma clang diagnostic push
#pragma ide diagnostic ignored "cert-err58-cpp"
        // MODULE PARAMETER DEFINITIONS:
        std::string const ObjName_ug_GeneralDomain("PipingSystem:Underground:Domain");
        std::string const ObjName_Circuit("PipingSystem:Underground:PipeCircuit");
        std::string const ObjName_Segment("PipingSystem:Underground:PipeSegment");
        std::string const ObjName_HorizTrench("GroundHeatExchanger:HorizontalTrench");
        std::string const ObjName_ZoneCoupled_Slab("Site:GroundDomain:Slab");
        std::string const ObjName_ZoneCoupled_Basement("Site:GroundDomain:Basement");

        // MODULE VARIABLE DECLARATIONS:
        std::vector<Domain> domains;
        std::vector<Circuit> circuits;
        std::vector<Segment> segments;
        std::unordered_map<std::string, std::string> GroundDomainUniqueNames;
        bool GetInputFlag(true); // First time, input is "gotten"
        bool GetSegmentInputFlag(true);
        bool WriteEIOFlag(true); // False after EIO is written
#pragma clang diagnostic pop

        void clear_state() {
            GetInputFlag = true;
            GetSegmentInputFlag = true;
            WriteEIOFlag = true;
            domains.clear();
            circuits.clear();
            segments.clear();
            GroundDomainUniqueNames.clear();
        }

        void CheckIfAnySlabs() {
            // SUBROUTINE INFORMATION:
            //       AUTHOR         Matt Mitchell
            //       DATE WRITTEN   May 2014
            //       MODIFIED       na
            //       RE-ENGINEERED  na
            int numSlabsCheck(inputProcessor->getNumObjectsFound(ObjName_ZoneCoupled_Slab));
            DataGlobals::AnySlabsInModel = (numSlabsCheck > 0);
        }

        void CheckIfAnyBasements() {
            // SUBROUTINE INFORMATION:
            //       AUTHOR         Matt Mitchell
            //       DATE WRITTEN   May 2014
            //       MODIFIED       na
            //       RE-ENGINEERED  na
            int const numBasementsCheck(inputProcessor->getNumObjectsFound(ObjName_ZoneCoupled_Basement));
            DataGlobals::AnyBasementsInModel = (numBasementsCheck > 0);
        }

        PlantComponent *Circuit::factory(int EP_UNUSED(objectType), std::string objectName) {
            // Process the input data for circuits if it hasn't been done already
            if (GetInputFlag) {
                GetPipingSystemsAndGroundDomainsInput();
                GetInputFlag = false;
            }
            // Now look for this particular pipe in the list
            for (auto &circuit : circuits) {
                if (circuit.Name == objectName) {
                    return &circuit;
                }
            }
            // If we didn't find it, fatal
            ShowFatalError(
                    "PipeCircuitInfoFactory: Error getting inputs for circuit named: " + objectName); // LCOV_EXCL_LINE
            // Shut up the compiler
            return nullptr; // LCOV_EXCL_LINE
        }

        void Circuit::simulate(const PlantLocation &EP_UNUSED(calledFromLocation),
                               bool const EP_UNUSED(FirstHVACIteration),
                               Real64 &EP_UNUSED(CurLoad),
                               bool const EP_UNUSED(RunFlag)) {
            // Retrieve the parent domain index for this pipe circuit
            auto &thisDomain(domains[this->ParentDomainIndex]);
            int CircuitNum = this->CircuitIndex;

            // Do any initialization here
            thisDomain.InitPipingSystems(CircuitNum);

            // Update the temperature field
            thisDomain.PerformIterationLoop(CircuitNum);

            // Update outlet nodes, etc.
            thisDomain.UpdatePipingSystems(CircuitNum);
        }

        void SimulateGroundDomains(bool initOnly) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Matt Mitchell
            //       DATE WRITTEN   Spring 2014
            //       MODIFIED       by Sushobhit Acharya, March 2015
            //       RE-ENGINEERED  na

            static std::string const RoutineName("InitAndSimGroundDomain");

            static gio::Fmt DomainCellsToEIOHeader(
                    "('! <Domain Name>, Total Number of Domain Cells, Total Number of Ground Surface Cells, Total Number of Insulation Cells')");
            static gio::Fmt DomainCellsToEIO("(A,',',I5',',I5',',I5)");

            // Read input if necessary
            if (GetInputFlag) {
                GetPipingSystemsAndGroundDomainsInput();
                GetInputFlag = false;
            }

            for (auto &thisDomain : domains) {

                // if the domain contains a pipe circuit, it shouldn't be initialized here, it has its own entry point
                if (thisDomain.HasAPipeCircuit) continue;

                if (thisDomain.DomainNeedsToBeMeshed) {
                    thisDomain.developMesh();
                }

                thisDomain.DomainNeedsToBeMeshed = false;

                // The time init should be done here before we DoOneTimeInits because the DoOneTimeInits
                // includes a ground temperature initialization, which is based on the Cur%CurSimTimeSeconds variable
                // which would be carried over from the previous environment
                thisDomain.Cur.CurSimTimeStepSize = DataGlobals::TimeStepZone * DataGlobals::SecInHour;
                thisDomain.Cur.CurSimTimeSeconds = ((DataGlobals::DayOfSim - 1) * 24 + (DataGlobals::HourOfDay - 1) +
                                                    (DataGlobals::TimeStep - 1) * DataGlobals::TimeStepZone +
                                                    DataHVACGlobals::SysTimeElapsed) * DataGlobals::SecInHour;

                // There are also some inits that are "close to one time" inits...( one-time in standalone, each envrn in E+ )
                if ((DataGlobals::BeginSimFlag && thisDomain.BeginSimInit) ||
                    (DataGlobals::BeginEnvrnFlag && thisDomain.BeginSimEnvironment)) {

                    thisDomain.DoOneTimeInitializations(-1);

                    if (thisDomain.HasZoneCoupledSlab) {
                        int Xmax = ubound(thisDomain.Cells, 1);
                        // int yMax = ubound( thisDomain.Cells, 2 );
                        int Zmax = ubound(thisDomain.Cells, 3);

                        thisDomain.WeightingFactor.allocate({0, Xmax}, {0, Zmax});
                        thisDomain.WeightedHeatFlux.allocate({0, Xmax}, {0, Zmax});
                    }

                    thisDomain.BeginSimInit = false;
                    thisDomain.BeginSimEnvironment = false;
                }
                if (!DataGlobals::BeginSimFlag) thisDomain.BeginSimInit = true;
                if (!DataGlobals::BeginEnvrnFlag) thisDomain.BeginSimEnvironment = true;

                // Reset the heat fluxes if domain update has been completed
                if (thisDomain.ResetHeatFluxFlag) {
                    thisDomain.AggregateHeatFlux = 0;
                    thisDomain.AggregateWallHeatFlux = 0;
                    thisDomain.AggregateFloorHeatFlux = 0;
                    thisDomain.NumHeatFlux = 0;
                    thisDomain.ResetHeatFluxFlag = false;
                }

                if (!initOnly) {
                    // Aggregate the heat flux
                    // Zone-coupled slab
                    if (thisDomain.HasZoneCoupledSlab) {
                        thisDomain.AggregateHeatFlux += thisDomain.GetZoneInterfaceHeatFlux();
                        thisDomain.NumHeatFlux += 1;
                        thisDomain.HeatFlux = thisDomain.AggregateHeatFlux / thisDomain.NumHeatFlux;
                    } else { // Coupled basement

                        // basement walls
                        thisDomain.AggregateWallHeatFlux += thisDomain.GetBasementWallHeatFlux();
                        // basement floor
                        thisDomain.AggregateFloorHeatFlux += thisDomain.GetBasementFloorHeatFlux();

                        thisDomain.NumHeatFlux += 1;
                        thisDomain.WallHeatFlux = thisDomain.AggregateWallHeatFlux / thisDomain.NumHeatFlux;
                        thisDomain.FloorHeatFlux = thisDomain.AggregateFloorHeatFlux / thisDomain.NumHeatFlux;
                    }

                    // Aggregate the heat flux
                    // Zone-coupled slab
                    if (thisDomain.HasZoneCoupledSlab) {
                        thisDomain.AggregateHeatFlux += thisDomain.GetZoneInterfaceHeatFlux();
                        thisDomain.NumHeatFlux += 1;
                        thisDomain.HeatFlux = thisDomain.AggregateHeatFlux / thisDomain.NumHeatFlux;
                    } else if (thisDomain.HasZoneCoupledBasement) { // Coupled basement
                        // basement walls
                        thisDomain.AggregateWallHeatFlux += thisDomain.GetBasementWallHeatFlux();
                        // basement floor
                        thisDomain.AggregateFloorHeatFlux += thisDomain.GetBasementFloorHeatFlux();

                        thisDomain.NumHeatFlux += 1;
                        thisDomain.WallHeatFlux = thisDomain.AggregateWallHeatFlux / thisDomain.NumHeatFlux;
                        thisDomain.FloorHeatFlux = thisDomain.AggregateFloorHeatFlux / thisDomain.NumHeatFlux;
                    }

                    // Zone-coupled slab
                    if (thisDomain.HasZoneCoupledSlab) {

                        thisDomain.HeatFlux = thisDomain.AggregateHeatFlux / thisDomain.NumHeatFlux;

                        Real64 ZoneTemp = 0.0;

                        // Set ZoneTemp equal to the average air temperature of the zones the coupled surfaces are part of.
                        for (auto &z: thisDomain.ZoneCoupledSurfaces) {
                            int ZoneNum = z.Zone;
                            ZoneTemp += DataHeatBalFanSys::ZTAV(ZoneNum);
                        }

                        ZoneTemp = ZoneTemp / thisDomain.ZoneCoupledSurfaces.size();
                        Real64 AvgSlabTemp = thisDomain.GetAverageTempByType(CellType::ZoneGroundInterface);

                        int yMax = ubound(thisDomain.Cells, 2);

                        for (int Z = lbound(thisDomain.Cells, 3); Z <= ubound(thisDomain.Cells, 3); ++Z) {
                            for (int X = lbound(thisDomain.Cells, 1); X <= ubound(thisDomain.Cells, 1); ++X) {
                                // Zone interface cells
                                if (thisDomain.Cells(X, yMax, Z).cellType == CellType::ZoneGroundInterface) {
                                    thisDomain.WeightingFactor(X, Z) =
                                            abs((ZoneTemp - thisDomain.Cells(X, yMax, Z).Temperature_PrevTimeStep) /
                                                (ZoneTemp - AvgSlabTemp));
                                }
                            }
                        }

                        // Set initial weighted heat flux
                        for (int Z = lbound(thisDomain.Cells, 3); Z <= ubound(thisDomain.Cells, 3); ++Z) {
                            for (int X = lbound(thisDomain.Cells, 1); X <= ubound(thisDomain.Cells, 1); ++X) {
                                // Zone interface cells
                                if (thisDomain.Cells(X, yMax, Z).cellType == CellType::ZoneGroundInterface) {
                                    thisDomain.WeightedHeatFlux(X, Z) =
                                            thisDomain.WeightingFactor(X, Z) * thisDomain.HeatFlux;
                                }
                            }
                        }

                        // Weighted heat flux and uniform heat flux balance energy may not balance exactly
                        // Calculate difference and adjust
                        thisDomain.TotalEnergyUniformHeatFlux =
                                thisDomain.HeatFlux * thisDomain.SlabArea * thisDomain.Cur.CurSimTimeStepSize;
                        thisDomain.TotalEnergyWeightedHeatFlux = 0.0;

                        for (int Z = lbound(thisDomain.Cells, 3); Z <= ubound(thisDomain.Cells, 3); ++Z) {
                            for (int X = lbound(thisDomain.Cells, 1); X <= ubound(thisDomain.Cells, 1); ++X) {
                                // Zone interface cells
                                if (thisDomain.Cells(X, yMax, Z).cellType == CellType::ZoneGroundInterface) {
                                    auto &cell(thisDomain.Cells(X, yMax, Z));
                                    thisDomain.TotalEnergyWeightedHeatFlux +=
                                            thisDomain.WeightedHeatFlux(X, Z) * cell.width() * cell.depth() *
                                            thisDomain.Cur.CurSimTimeStepSize;
                                }
                            }
                        }

                        thisDomain.HeatFluxWeightingFactor =
                                thisDomain.TotalEnergyWeightedHeatFlux / thisDomain.TotalEnergyUniformHeatFlux;
                        thisDomain.TotalEnergyWeightedHeatFlux = 0.0;

                        // Finally, adjust the weighted heat flux so that energy balances
                        for (int Z = lbound(thisDomain.Cells, 3); Z <= ubound(thisDomain.Cells, 3); ++Z) {
                            for (int X = lbound(thisDomain.Cells, 1); X <= ubound(thisDomain.Cells, 1); ++X) {
                                // Zone interface cells
                                if (thisDomain.Cells(X, yMax, Z).cellType == CellType::ZoneGroundInterface) {
                                    auto &cell(thisDomain.Cells(X, yMax, Z));
                                    thisDomain.WeightedHeatFlux(X, Z) =
                                            thisDomain.WeightedHeatFlux(X, Z) / thisDomain.HeatFluxWeightingFactor;
                                    thisDomain.TotalEnergyWeightedHeatFlux +=
                                            thisDomain.WeightedHeatFlux(X, Z) * cell.width() * cell.depth() *
                                            thisDomain.Cur.CurSimTimeStepSize;
                                }
                            }
                        }

                    } else { // Coupled basement
                        thisDomain.WallHeatFlux = thisDomain.AggregateWallHeatFlux / thisDomain.NumHeatFlux;
                        thisDomain.FloorHeatFlux = thisDomain.AggregateFloorHeatFlux / thisDomain.NumHeatFlux;
                    }

                    // Shift history arrays only if necessary
                    if (std::abs(thisDomain.Cur.CurSimTimeSeconds - thisDomain.Cur.PrevSimTimeSeconds) > 1.0e-6) {
                        thisDomain.Cur.PrevSimTimeSeconds = thisDomain.Cur.CurSimTimeSeconds;
                        thisDomain.ShiftTemperaturesForNewTimeStep();
                        thisDomain.DomainNeedsSimulation = true;
                    }
                    thisDomain.PerformIterationLoop(-1);
                }
            }

            if (WriteEIOFlag) {
                // Write eio header
                gio::write(DataGlobals::OutputFileInits, DomainCellsToEIOHeader);

                // Write eio data
                for (auto &thisDomain : domains) {
                    gio::write(DataGlobals::OutputFileInits, DomainCellsToEIO)
                            << thisDomain.Name << thisDomain.NumDomainCells << thisDomain.NumGroundSurfCells
                            << thisDomain.NumInsulationCells;
                }
                WriteEIOFlag = false;
            }
        }

        void GetPipingSystemsAndGroundDomainsInput() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            static std::string const RoutineName("GetPipingSystemsAndGroundDomainsInput");

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            bool ErrorsFound(false); // Set to true if errors in input, fatal at end of routine

            // Read number of objects and allocate main data structures - first domains
            int NumGeneralizedDomains = inputProcessor->getNumObjectsFound(ObjName_ug_GeneralDomain);
            int NumHorizontalTrenches = inputProcessor->getNumObjectsFound(ObjName_HorizTrench);
            int NumZoneCoupledDomains = inputProcessor->getNumObjectsFound(ObjName_ZoneCoupled_Slab);
            int NumBasements = inputProcessor->getNumObjectsFound(ObjName_ZoneCoupled_Basement);
            int TotalNumDomains = NumGeneralizedDomains + NumHorizontalTrenches + NumZoneCoupledDomains + NumBasements;
            domains.resize(TotalNumDomains);

            // then circuits
            int NumPipeCircuits = inputProcessor->getNumObjectsFound(ObjName_Circuit);
            int TotalNumCircuits = NumPipeCircuits + NumHorizontalTrenches;
            circuits.resize(TotalNumCircuits);

            // Read in raw inputs, don't try to interpret dependencies yet
            ReadGeneralDomainInputs(1, NumGeneralizedDomains, ErrorsFound);
            ReadPipeCircuitInputs(NumPipeCircuits, ErrorsFound);
            ReadHorizontalTrenchInputs(NumGeneralizedDomains + 1, NumPipeCircuits + 1, ErrorsFound);

            // This is heavily dependent on the order of the domains in the main array.
            ReadZoneCoupledDomainInputs(NumGeneralizedDomains + NumHorizontalTrenches + 1, NumZoneCoupledDomains,
                                        ErrorsFound);

            // This is heavily dependent on the order of the domains in the main array.
            ReadBasementInputs(NumGeneralizedDomains + NumHorizontalTrenches + NumZoneCoupledDomains + 1, NumBasements,
                               ErrorsFound);

            // Report errors that are purely input problems
            if (ErrorsFound) ShowFatalError(RoutineName + ": Preceding input errors cause program termination.");

            // Setup output variables
            SetupPipingSystemOutputVariables();

            // Validate DOMAIN-CIRCUIT cross references
            for (int DomainNum = 0; DomainNum < TotalNumDomains; ++DomainNum) {

                // Convenience
                int const NumCircuitsInThisDomain = static_cast<int>(domains[DomainNum].CircuitNames.size());

                // validate pipe domain-circuit name-to-index references
                for (int CircuitCtr = 1; CircuitCtr <= NumCircuitsInThisDomain; ++CircuitCtr) {
                    std::string thisCircuitName = domains[DomainNum].CircuitNames[CircuitCtr - 1];
                    auto tryToFindCircuitByName = std::find(circuits.begin(), circuits.end(), thisCircuitName);
                    if (tryToFindCircuitByName == circuits.end()) {
                        // error, could not find domain-specified-circuit-name in the list of input circuits
                    } else {
                        int thisCircuitIndex = (int) std::distance(circuits.begin(), tryToFindCircuitByName);
                        domains[DomainNum].CircuitIndices.push_back(thisCircuitIndex);
                        circuits[thisCircuitIndex].ParentDomainIndex = DomainNum;
                    }
                }

                // correct segment locations for: INTERNAL DATA STRUCTURE Y VALUE MEASURED FROM BOTTOM OF DOMAIN,
                //                                INPUT WAS MEASURED FROM GROUND SURFACE
                for (int CircuitCtr = 1; CircuitCtr <= NumCircuitsInThisDomain; ++CircuitCtr) {
                    int const CircuitIndex = domains[DomainNum].CircuitIndices[CircuitCtr - 1];
                    for (int PipeCtr = 0;
                         PipeCtr < static_cast<int>(circuits[CircuitIndex].pipeSegments.size()); ++PipeCtr) {
                        auto &thisSegment = circuits[CircuitCtr - 1].pipeSegments[PipeCtr];
                        thisSegment->PipeLocation.Y = domains[DomainNum].Extents.yMax - thisSegment->PipeLocation.Y;
                    } // segment loop
                }     // circuit loop

                // correct segment locations for: BASEMENT X SHIFT
                if (domains[DomainNum].HasBasement && domains[DomainNum].BasementZone.ShiftPipesByWidth) {
                    for (int CircuitCtr = 1; CircuitCtr <= NumCircuitsInThisDomain; ++CircuitCtr) {
                        int const CircuitIndex = domains[DomainNum].CircuitIndices[CircuitCtr - 1];
                        for (int PipeCtr = 0;
                             PipeCtr < static_cast<int>(circuits[CircuitIndex].pipeSegments.size()); ++PipeCtr) {
                            auto &thisSegment = circuits[CircuitIndex].pipeSegments[PipeCtr];
                            thisSegment->PipeLocation.X += domains[DomainNum].BasementZone.Width;
                        } // segment loop
                    }     // circuit loop
                }

                // now we will have good values of pipe segment locations, we can validate them
                for (int CircuitCtr = 1; CircuitCtr <= NumCircuitsInThisDomain; ++CircuitCtr) {

                    // retrieve the index
                    int const CircuitIndex = domains[DomainNum].CircuitIndices[CircuitCtr - 1];

                    // check to make sure it isn't outside the domain
                    for (int PipeCtr = 0;
                         PipeCtr < static_cast<int>(circuits[CircuitIndex].pipeSegments.size()); ++PipeCtr) {
                        auto &thisSegment = circuits[CircuitIndex].pipeSegments[PipeCtr];
                        if ((thisSegment->PipeLocation.X > domains[DomainNum].Extents.xMax) ||
                            (thisSegment->PipeLocation.X < 0.0) ||
                            (thisSegment->PipeLocation.Y > domains[DomainNum].Extents.yMax) ||
                            (thisSegment->PipeLocation.Y < 0.0)) {
                            ShowSevereError(
                                    "PipingSystems::" + RoutineName +
                                    ":A pipe was found to be outside of the domain extents after performing any corrections for basement or burial depth.");
                            ShowContinueError("Pipe segment name:" + thisSegment->Name);
                            ShowContinueError("Corrected pipe location: ( x,y )=( " +
                                              General::TrimSigDigits(thisSegment->PipeLocation.X, 2) + ',' +
                                              General::TrimSigDigits(thisSegment->PipeLocation.Y, 2) + " )");
                        }
                    } // segment loop

                } // circuit loop

            } // domain loop

            // If we encountered any other errors that we couldn't handle separately than stop now
            if (ErrorsFound) {
                ShowFatalError(RoutineName + ':' + ObjName_ug_GeneralDomain + ": Errors found in input.");
            }
        }

        void ReadGeneralDomainInputs(int const IndexStart, int const NumGeneralizedDomains, bool &ErrorsFound) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            static std::string const RoutineName("ReadGeneralDomainInputs");

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            int NumAlphas;  // Number of Alphas for each GetObjectItem call
            int NumNumbers; // Number of Numbers for each GetObjectItem call
            int IOStatus;   // Used in GetObjectItem
            int CurIndex;

            for (int DomainNum = IndexStart; DomainNum <= NumGeneralizedDomains; ++DomainNum) {

                // Set up all the inputs for this domain object
                inputProcessor->getObjectItem(ObjName_ug_GeneralDomain,
                                              DomainNum,
                                              DataIPShortCuts::cAlphaArgs,
                                              NumAlphas,
                                              DataIPShortCuts::rNumericArgs,
                                              NumNumbers,
                                              IOStatus,
                                              DataIPShortCuts::lNumericFieldBlanks,
                                              DataIPShortCuts::lAlphaFieldBlanks,
                                              DataIPShortCuts::cAlphaFieldNames,
                                              DataIPShortCuts::cNumericFieldNames);

                auto &thisDomain = domains[DomainNum - 1];

                // Get the name, validate
                thisDomain.Name = DataIPShortCuts::cAlphaArgs(1);
                UtilityRoutines::IsNameEmpty(DataIPShortCuts::cAlphaArgs(1), DataIPShortCuts::cCurrentModuleObject,
                                             ErrorsFound);
                // Mesh extents, validated by IP
                thisDomain.Extents.xMax = DataIPShortCuts::rNumericArgs(1);
                thisDomain.Extents.yMax = DataIPShortCuts::rNumericArgs(2);
                thisDomain.Extents.zMax = DataIPShortCuts::rNumericArgs(3);

                // X direction mesh inputs, validated by IP
                thisDomain.Mesh.X.RegionMeshCount = static_cast<int>(DataIPShortCuts::rNumericArgs(4));
                {
                    auto const meshDistribution(uppercased(DataIPShortCuts::cAlphaArgs(2)));
                    if (meshDistribution == "UNIFORM") {
                        thisDomain.Mesh.X.thisMeshDistribution = MeshDistribution::Uniform;
                    } else if (meshDistribution == "SYMMETRICGEOMETRIC") {
                        thisDomain.Mesh.X.thisMeshDistribution = MeshDistribution::SymmetricGeometric;
                        if (mod(thisDomain.Mesh.X.RegionMeshCount, 2) != 0) {
                            ShowWarningError("PipingSystems:" + RoutineName + ": Invalid mesh type-count combination.");
                            ShowContinueError("Instance:" + ObjName_ug_GeneralDomain + '=' + thisDomain.Name);
                            ShowContinueError(
                                    "An ODD-valued X mesh count was found in the input for symmetric geometric configuration.");
                            ShowContinueError("This is invalid, mesh count incremented UP by one to next EVEN value.");
                            ++thisDomain.Mesh.X.RegionMeshCount;
                            thisDomain.Mesh.X.GeometricSeriesCoefficient = DataIPShortCuts::rNumericArgs(5);
                        } else {
                            thisDomain.Mesh.X.GeometricSeriesCoefficient = 1.0;
                        }
                    } else {
                        IssueSevereInputFieldError(RoutineName,
                                                   ObjName_ug_GeneralDomain,
                                                   DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cAlphaFieldNames(2),
                                                   DataIPShortCuts::cAlphaArgs(2),
                                                   "Use a choice from the available mesh type keys.",
                                                   ErrorsFound);
                    }
                }

                // Y direction mesh inputs, validated by IP
                thisDomain.Mesh.Y.RegionMeshCount = static_cast<int>(DataIPShortCuts::rNumericArgs(6));
                {
                    auto const meshDistribution(stripped(DataIPShortCuts::cAlphaArgs(3)));
                    if (meshDistribution == "UNIFORM") {
                        thisDomain.Mesh.Y.thisMeshDistribution = MeshDistribution::Uniform;
                    } else if (meshDistribution == "SYMMETRICGEOMETRIC") {
                        thisDomain.Mesh.Y.thisMeshDistribution = MeshDistribution::SymmetricGeometric;
                        if (mod(thisDomain.Mesh.Y.RegionMeshCount, 2) != 0) {
                            ShowWarningError("PipingSystems:" + RoutineName + ": Invalid mesh type-count combination.");
                            ShowContinueError("Instance:" + ObjName_ug_GeneralDomain + '=' + thisDomain.Name);
                            ShowContinueError(
                                    "An ODD-valued Y mesh count was found in the input for symmetric geometric configuration.");
                            ShowContinueError("This is invalid, mesh count incremented UP by one to next EVEN value.");
                            ++thisDomain.Mesh.Y.RegionMeshCount;
                            thisDomain.Mesh.Y.GeometricSeriesCoefficient = DataIPShortCuts::rNumericArgs(7);
                        } else {
                            thisDomain.Mesh.Y.GeometricSeriesCoefficient = 1.0;
                        }
                    } else {
                        IssueSevereInputFieldError(RoutineName,
                                                   ObjName_ug_GeneralDomain,
                                                   DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cAlphaFieldNames(3),
                                                   DataIPShortCuts::cAlphaArgs(3),
                                                   "Use a choice from the available mesh type keys.",
                                                   ErrorsFound);
                    }
                }

                // Z direction mesh inputs, validated by IP
                thisDomain.Mesh.Z.RegionMeshCount = static_cast<int>(DataIPShortCuts::rNumericArgs(8));
                {
                    auto const meshDistribution(stripped(DataIPShortCuts::cAlphaArgs(4)));
                    if (meshDistribution == "UNIFORM") {
                        thisDomain.Mesh.Z.thisMeshDistribution = MeshDistribution::Uniform;
                    } else if (meshDistribution == "SYMMETRICGEOMETRIC") {
                        thisDomain.Mesh.Z.thisMeshDistribution = MeshDistribution::SymmetricGeometric;
                        if (mod(thisDomain.Mesh.Z.RegionMeshCount, 2) != 0) {
                            ShowWarningError("PipingSystems:" + RoutineName + ": Invalid mesh type-count combination.");
                            ShowContinueError("Instance:" + ObjName_ug_GeneralDomain + '=' + thisDomain.Name);
                            ShowContinueError(
                                    "An ODD-valued Z mesh count was found in the input for symmetric geometric configuration.");
                            ShowContinueError("This is invalid, mesh count incremented UP by one to next EVEN value.");
                            ++thisDomain.Mesh.Z.RegionMeshCount;
                            thisDomain.Mesh.Z.GeometricSeriesCoefficient = DataIPShortCuts::rNumericArgs(9);
                        } else {
                            thisDomain.Mesh.Z.GeometricSeriesCoefficient = 1.0;
                        }
                    } else {
                        IssueSevereInputFieldError(RoutineName,
                                                   ObjName_ug_GeneralDomain,
                                                   DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cAlphaFieldNames(4),
                                                   DataIPShortCuts::cAlphaArgs(4),
                                                   "Use a choice from the available mesh type keys.",
                                                   ErrorsFound);
                    }
                }

                // Soil properties, validated min/max by IP
                thisDomain.GroundProperties.Conductivity = DataIPShortCuts::rNumericArgs(10);
                thisDomain.GroundProperties.Density = DataIPShortCuts::rNumericArgs(11);
                thisDomain.GroundProperties.SpecificHeat = DataIPShortCuts::rNumericArgs(12);

                // Moisture properties, validated min/max by IP, and converted to a fraction for computation here
                thisDomain.Moisture.Theta_liq = DataIPShortCuts::rNumericArgs(13) / 100.0;
                thisDomain.Moisture.Theta_sat = DataIPShortCuts::rNumericArgs(14) / 100.0;

                // check if there is a basement
                if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(7), "YES")) {
                    thisDomain.HasBasement = true;
                } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(7), "NO")) {
                    thisDomain.HasBasement = false;
                } else {
                    IssueSevereInputFieldError(RoutineName,
                                               ObjName_ug_GeneralDomain,
                                               DataIPShortCuts::cAlphaArgs(1),
                                               DataIPShortCuts::cAlphaFieldNames(7),
                                               DataIPShortCuts::cAlphaArgs(7),
                                               "Must enter either yes or no.",
                                               ErrorsFound);
                }

                // more work to do if there is a basement
                if (thisDomain.HasBasement) {

                    // check if there are blank inputs related to the basement,
                    // IP can't catch this because they are inherently optional if there ISN'T a basement
                    if (DataIPShortCuts::lNumericFieldBlanks(15) || DataIPShortCuts::lNumericFieldBlanks(16) ||
                        DataIPShortCuts::lAlphaFieldBlanks(8) || DataIPShortCuts::lAlphaFieldBlanks(9) ||
                        DataIPShortCuts::lAlphaFieldBlanks(10)) {
                        ShowSevereError("Erroneous basement inputs for " + ObjName_ug_GeneralDomain + '=' +
                                        DataIPShortCuts::cAlphaArgs(1));
                        ShowContinueError(
                                "Object specified to have a basement, while at least one basement input was left blank.");
                        ErrorsFound = true;
                    }

                    // get dimensions for meshing
                    CurIndex = 15;
                    thisDomain.BasementZone.Width = DataIPShortCuts::rNumericArgs(CurIndex);
                    if (thisDomain.BasementZone.Width <= 0.0) {
                        IssueSevereInputFieldError(RoutineName,
                                                   ObjName_ug_GeneralDomain,
                                                   DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cNumericFieldNames(CurIndex),
                                                   DataIPShortCuts::rNumericArgs(CurIndex),
                                                   "Basement width must be a positive nonzero value.",
                                                   ErrorsFound);
                    }

                    CurIndex = 16;
                    thisDomain.BasementZone.Depth = DataIPShortCuts::rNumericArgs(CurIndex);
                    if (thisDomain.BasementZone.Depth <= 0.0) {
                        IssueSevereInputFieldError(RoutineName,
                                                   ObjName_ug_GeneralDomain,
                                                   DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cNumericFieldNames(CurIndex),
                                                   DataIPShortCuts::rNumericArgs(CurIndex),
                                                   "Basement depth must be a positive nonzero value.",
                                                   ErrorsFound);
                    }

                    // check for dimension shift
                    CurIndex = 8;
                    if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(CurIndex), "YES")) {
                        thisDomain.BasementZone.ShiftPipesByWidth = true;
                    } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(CurIndex), "NO")) {
                        thisDomain.BasementZone.ShiftPipesByWidth = false;
                    } else {
                        IssueSevereInputFieldError(RoutineName,
                                                   ObjName_ug_GeneralDomain,
                                                   DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                                   DataIPShortCuts::cAlphaArgs(CurIndex),
                                                   "Must enter either yes or no.",
                                                   ErrorsFound);
                    }

                    // get boundary condition model names and indices --error check
                    CurIndex = 9;
                    thisDomain.BasementZone.WallBoundaryOSCMName = DataIPShortCuts::cAlphaArgs(CurIndex);
                    thisDomain.BasementZone.WallBoundaryOSCMIndex =
                            UtilityRoutines::FindItemInList(thisDomain.BasementZone.WallBoundaryOSCMName,
                                                            DataSurfaces::OSCM);
                    if (thisDomain.BasementZone.WallBoundaryOSCMIndex <= 0) {
                        IssueSevereInputFieldError(RoutineName,
                                                   ObjName_ug_GeneralDomain,
                                                   DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                                   DataIPShortCuts::cAlphaArgs(CurIndex),
                                                   "Could not match with an Other Side Conditions Model input object.",
                                                   ErrorsFound);
                    } else {
                        auto const &wallIndexes = GetSurfaceIndecesForOSCM(
                                thisDomain.BasementZone.WallBoundaryOSCMIndex);
                        if (wallIndexes.empty()) {
                            IssueSevereInputFieldError(
                                    RoutineName,
                                    ObjName_ug_GeneralDomain,
                                    DataIPShortCuts::cAlphaArgs(1),
                                    DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                    DataIPShortCuts::cAlphaArgs(CurIndex),
                                    "Entry matched an Other Side Conditions Model, but no surfaces were found to be using this Other Side Conditions Model.",
                                    ErrorsFound);
                        } else {
                            thisDomain.BasementZone.WallSurfacePointers = wallIndexes;
                        }
                    }

                    CurIndex = 10;
                    thisDomain.BasementZone.FloorBoundaryOSCMName = DataIPShortCuts::cAlphaArgs(CurIndex);
                    thisDomain.BasementZone.FloorBoundaryOSCMIndex =
                            UtilityRoutines::FindItemInList(thisDomain.BasementZone.FloorBoundaryOSCMName,
                                                            DataSurfaces::OSCM);
                    if (thisDomain.BasementZone.FloorBoundaryOSCMIndex <= 0) {
                        IssueSevereInputFieldError(RoutineName,
                                                   ObjName_ug_GeneralDomain,
                                                   DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                                   DataIPShortCuts::cAlphaArgs(CurIndex),
                                                   "Could not match with an Other Side Conditions Model input object.",
                                                   ErrorsFound);
                    } else {
                        auto const &floorIndexes = GetSurfaceIndecesForOSCM(
                                thisDomain.BasementZone.FloorBoundaryOSCMIndex);
                        if (floorIndexes.empty()) {
                            IssueSevereInputFieldError(
                                    RoutineName,
                                    ObjName_ug_GeneralDomain,
                                    DataIPShortCuts::cAlphaArgs(1),
                                    DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                    DataIPShortCuts::cAlphaArgs(CurIndex),
                                    "Entry matched an Other Side Conditions Model, but no surfaces were found to be using this Other Side Conditions Model.",
                                    ErrorsFound);
                        } else {
                            thisDomain.BasementZone.FloorSurfacePointers = floorIndexes;
                        }
                    }
                }

                // get some convergence tolerances, minimum/maximum are enforced by the IP, along with default values if user left them blank
                thisDomain.SimControls.Convergence_CurrentToPrevIteration = DataIPShortCuts::rNumericArgs(17);
                thisDomain.SimControls.MaxIterationsPerTS = static_cast<int>(DataIPShortCuts::rNumericArgs(18));

                // additional evapotranspiration parameter, min/max validated by IP
                thisDomain.Moisture.GroundCoverCoefficient = DataIPShortCuts::rNumericArgs(19);

                // Allocate the circuit placeholder arrays
                int const NumCircuitsInThisDomain = int(DataIPShortCuts::rNumericArgs(20));

                // Check for blank or missing or mismatched number...
                int const NumAlphasBeforePipeCircOne = 10;
                for (int CircuitCtr = 1; CircuitCtr <= NumCircuitsInThisDomain; ++CircuitCtr) {
                    thisDomain.CircuitNames.push_back(
                            DataIPShortCuts::cAlphaArgs(CircuitCtr + NumAlphasBeforePipeCircOne));
                }

                // Initialize ground temperature model and get pointer reference
                thisDomain.Farfield.groundTempModel = GetGroundTempModelAndInit(DataIPShortCuts::cAlphaArgs(5),
                                                                                DataIPShortCuts::cAlphaArgs(6));

                // now add this instance to the main vector
                // domains.push_back(thisDomain);
            }
        }

        void ReadZoneCoupledDomainInputs(int const StartingDomainNumForZone, int const NumZoneCoupledDomains,
                                         bool &ErrorsFound) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       Spring 2014 by Matt Mitchell and Sushobhit Acharya to accommodate ground coupled calculations
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            static std::string const RoutineName("ReadZoneCoupledDomainInputs");

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            int NumAlphas;  // Number of Alphas for each GetObjectItem call
            int NumNumbers; // Number of Numbers for each GetObjectItem call
            int IOStatus;   // Used in GetObjectItem

            struct GroundDomainData {
                // Members
                std::string ObjName;
                Real64 Depth;
                Real64 AspectRatio;
                Real64 PerimeterOffset;
                Real64 SoilConductivity;
                Real64 SoilDensity;
                Real64 SoilSpecificHeat;
                Real64 MoistureContent;
                Real64 SaturationMoistureContent;
                Real64 EvapotranspirationCoeff;
                Real64 MinSurfTemp;
                int MonthOfMinSurfTemp;
                Real64 HorizInsWidth;
                Real64 VertInsDepth;
                int OSCMIndex;
                std::string OSCMName;
                std::string SlabMaterial;
                std::string HorizInsMaterial;
                std::string VertInsMaterial;

                // Default Constructor
                GroundDomainData()
                        : Depth(0.0), AspectRatio(0.0), PerimeterOffset(0.0), SoilConductivity(0.0), SoilDensity(0.0),
                          SoilSpecificHeat(0.0),
                          MoistureContent(0.0), SaturationMoistureContent(0.0), EvapotranspirationCoeff(0.0),
                          MinSurfTemp(0.0), MonthOfMinSurfTemp(0),
                          HorizInsWidth(0.0), VertInsDepth(0.0), OSCMIndex(0) {
                }
            };

            // initialize these counters properly so they can be incremented within the DO loop
            int DomainCtr = StartingDomainNumForZone - 1;

            // For each domain, we need to process the inputs into a local array of derived type, then resolve each one, creating definitions for a zone
            // coupled domain. This way, the outer get input routines can handle it as though they were generalized routines

            for (int ZoneCoupledDomainCtr = 1; ZoneCoupledDomainCtr <= NumZoneCoupledDomains; ++ZoneCoupledDomainCtr) {

                // Increment the domain counters here
                ++DomainCtr;

                // Read all the inputs for this domain object
                inputProcessor->getObjectItem(ObjName_ZoneCoupled_Slab,
                                              ZoneCoupledDomainCtr,
                                              DataIPShortCuts::cAlphaArgs,
                                              NumAlphas,
                                              DataIPShortCuts::rNumericArgs,
                                              NumNumbers,
                                              IOStatus,
                                              DataIPShortCuts::lNumericFieldBlanks,
                                              DataIPShortCuts::lAlphaFieldBlanks,
                                              DataIPShortCuts::cAlphaFieldNames,
                                              DataIPShortCuts::cNumericFieldNames);

                GroundDomainData gdd;

                // Get the name, validate
                gdd.ObjName = DataIPShortCuts::cAlphaArgs(1);
                GlobalNames::VerifyUniqueInterObjectName(
                        GroundDomainUniqueNames, DataIPShortCuts::cAlphaArgs(1), ObjName_ZoneCoupled_Slab,
                        DataIPShortCuts::cAlphaFieldNames(1), ErrorsFound);

                // Read in the rest of the inputs into the local type for clarity during transition
                gdd.OSCMName = DataIPShortCuts::cAlphaArgs(4);
                gdd.Depth = DataIPShortCuts::rNumericArgs(1);
                gdd.AspectRatio = DataIPShortCuts::rNumericArgs(2);
                gdd.PerimeterOffset = DataIPShortCuts::rNumericArgs(3);
                gdd.SoilConductivity = DataIPShortCuts::rNumericArgs(4);
                gdd.SoilDensity = DataIPShortCuts::rNumericArgs(5);
                gdd.SoilSpecificHeat = DataIPShortCuts::rNumericArgs(6);
                gdd.MoistureContent = DataIPShortCuts::rNumericArgs(7);
                gdd.SaturationMoistureContent = DataIPShortCuts::rNumericArgs(8);
                gdd.EvapotranspirationCoeff = DataIPShortCuts::rNumericArgs(9);
                gdd.HorizInsWidth = DataIPShortCuts::rNumericArgs(10);
                gdd.VertInsDepth = DataIPShortCuts::rNumericArgs(11);

                auto &thisDomain = domains[DomainCtr - 1];

                // Set flag for slab in-grade or slab on-grade
                if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(5), "INGRADE")) {
                    thisDomain.SlabInGradeFlag = true;
                } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(5), "ONGRADE")) {
                    thisDomain.SlabInGradeFlag = false;
                } else {
                    ShowSevereError(
                            "Invalid " + DataIPShortCuts::cAlphaFieldNames(5) + "=" + DataIPShortCuts::cAlphaArgs(5));
                    ShowContinueError("Found in: " + gdd.ObjName);
                    ErrorsFound = true;
                }

                // Get slab material properties
                if (thisDomain.SlabInGradeFlag) {
                    gdd.SlabMaterial = DataIPShortCuts::cAlphaArgs(6);
                    thisDomain.SlabMaterialNum = UtilityRoutines::FindItemInList(DataIPShortCuts::cAlphaArgs(6),
                                                                                 DataHeatBalance::Material,
                                                                                 DataHeatBalance::TotMaterials);
                    if (thisDomain.SlabMaterialNum == 0) {
                        ShowSevereError("Invalid " + DataIPShortCuts::cAlphaFieldNames(6) + "=" +
                                        DataIPShortCuts::cAlphaArgs(6));
                        ShowContinueError("Found in: " + gdd.ObjName);
                        ErrorsFound = true;
                    } else {
                        thisDomain.SlabThickness = DataHeatBalance::Material(thisDomain.SlabMaterialNum).Thickness;
                        thisDomain.SlabProperties.Density = DataHeatBalance::Material(
                                thisDomain.SlabMaterialNum).Density;
                        thisDomain.SlabProperties.SpecificHeat = DataHeatBalance::Material(
                                thisDomain.SlabMaterialNum).SpecHeat;
                        thisDomain.SlabProperties.Conductivity = DataHeatBalance::Material(
                                thisDomain.SlabMaterialNum).Conductivity;
                    }
                }

                // set flag for horizontal insulation
                if (thisDomain.SlabInGradeFlag) {
                    if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(7), "NO")) {
                        thisDomain.HorizInsPresentFlag = false;
                    } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(7), "YES")) {
                        thisDomain.HorizInsPresentFlag = true;
                    } else {
                        ShowSevereError("Invalid " + DataIPShortCuts::cAlphaFieldNames(7) + "=" +
                                        DataIPShortCuts::cAlphaArgs(7));
                        ShowContinueError("Found in: " + gdd.ObjName);
                        ErrorsFound = true;
                    }
                }

                // Get horizontal insulation material properties
                if (thisDomain.HorizInsPresentFlag) {
                    gdd.HorizInsMaterial = DataIPShortCuts::cAlphaArgs(8);
                    thisDomain.HorizInsMaterialNum = UtilityRoutines::FindItemInList(DataIPShortCuts::cAlphaArgs(8),
                                                                                     DataHeatBalance::Material,
                                                                                     DataHeatBalance::TotMaterials);
                    if (thisDomain.HorizInsMaterialNum == 0) {
                        ShowSevereError("Invalid " + DataIPShortCuts::cAlphaFieldNames(8) + "=" +
                                        DataIPShortCuts::cAlphaArgs(8));
                        ShowContinueError("Found in: " + gdd.ObjName);
                        ErrorsFound = true;
                    } else {
                        thisDomain.HorizInsThickness = DataHeatBalance::Material(
                                thisDomain.HorizInsMaterialNum).Thickness;
                        thisDomain.HorizInsProperties.Density = DataHeatBalance::Material(
                                thisDomain.HorizInsMaterialNum).Density;
                        thisDomain.HorizInsProperties.SpecificHeat = DataHeatBalance::Material(
                                thisDomain.HorizInsMaterialNum).SpecHeat;
                        thisDomain.HorizInsProperties.Conductivity = DataHeatBalance::Material(
                                thisDomain.HorizInsMaterialNum).Conductivity;
                    }

                    // Set flag for horizontal insulation extents
                    if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(9), "PERIMETER")) {
                        thisDomain.FullHorizInsPresent = false;
                        // Horizontal insulation perimeter width
                        if (gdd.HorizInsWidth > 0.0) {
                            thisDomain.HorizInsWidth = gdd.HorizInsWidth;
                        } else {
                            ShowSevereError("Invalid " + DataIPShortCuts::cNumericFieldNames(10));
                            ShowContinueError("Found in: " + gdd.ObjName);
                            ErrorsFound = true;
                        }
                    } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(9), "FULL")) {
                        thisDomain.FullHorizInsPresent = true;
                    } else {
                        ShowSevereError("Invalid " + DataIPShortCuts::cAlphaFieldNames(9) + "=" +
                                        DataIPShortCuts::cAlphaArgs(9));
                        ShowContinueError("Found in: " + gdd.ObjName);
                        ErrorsFound = true;
                    }

                    // Horizontal insulation perimeter width
                    thisDomain.HorizInsWidth = gdd.HorizInsWidth;
                }

                // set flag for vertical insulation
                if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(10), "NO")) {
                    thisDomain.VertInsPresentFlag = false;
                } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(10), "YES")) {
                    thisDomain.VertInsPresentFlag = true;
                } else {
                    ShowSevereError(
                            "Invalid " + DataIPShortCuts::cAlphaFieldNames(10) + "=" + DataIPShortCuts::cAlphaArgs(10));
                    ShowContinueError("Found in: " + gdd.ObjName);
                    ErrorsFound = true;
                }

                // Get vertical insulation material properties
                if (thisDomain.VertInsPresentFlag) {
                    gdd.VertInsMaterial = DataIPShortCuts::cAlphaArgs(11);
                    thisDomain.VertInsMaterialNum = UtilityRoutines::FindItemInList(DataIPShortCuts::cAlphaArgs(11),
                                                                                    DataHeatBalance::Material,
                                                                                    DataHeatBalance::TotMaterials);
                    if (thisDomain.VertInsMaterialNum == 0) {
                        ShowSevereError("Invalid " + DataIPShortCuts::cAlphaFieldNames(11) + "=" +
                                        DataIPShortCuts::cAlphaArgs(11));
                        ShowContinueError("Found in: " + gdd.ObjName);
                        ErrorsFound = true;
                    } else {
                        thisDomain.VertInsThickness = DataHeatBalance::Material(
                                thisDomain.VertInsMaterialNum).Thickness;
                        thisDomain.VertInsProperties.Density = DataHeatBalance::Material(
                                thisDomain.VertInsMaterialNum).Density;
                        thisDomain.VertInsProperties.SpecificHeat = DataHeatBalance::Material(
                                thisDomain.VertInsMaterialNum).SpecHeat;
                        thisDomain.VertInsProperties.Conductivity = DataHeatBalance::Material(
                                thisDomain.VertInsMaterialNum).Conductivity;
                    }

                    // vertical insulation depth
                    if (gdd.VertInsDepth > gdd.Depth ||
                        gdd.VertInsDepth <= 0.0) {
                        ShowSevereError("Invalid " + DataIPShortCuts::cNumericFieldNames(11));
                        ShowContinueError("Found in: " + gdd.ObjName);
                        ErrorsFound = true;
                    } else {
                        thisDomain.VertInsDepth = gdd.VertInsDepth;
                    }
                }

                // Domain perimeter offset
                thisDomain.PerimeterOffset = gdd.PerimeterOffset;

                // Set simulation interval flag
                if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(12), "TIMESTEP")) {
                    thisDomain.SimTimeStepFlag = true;
                } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(12), "HOURLY")) {
                    thisDomain.SimHourlyFlag = true;
                } else {
                    ShowSevereError(
                            "Invalid " + DataIPShortCuts::cAlphaFieldNames(12) + "=" + DataIPShortCuts::cAlphaArgs(12));
                    ShowContinueError("Found in: " + gdd.ObjName);
                    ErrorsFound = true;
                }

                //******* We'll first set up the domain ********
                thisDomain.IsActuallyPartOfAHorizontalTrench = false;
                thisDomain.HasAPipeCircuit = false;
                thisDomain.HasZoneCoupledSlab = true;

                // Domain name
                thisDomain.Name = gdd.ObjName;

                // get boundary condition model names and indices -- error check
                thisDomain.ZoneCoupledOSCMIndex = UtilityRoutines::FindItemInList(gdd.OSCMName, DataSurfaces::OSCM);
                if (thisDomain.ZoneCoupledOSCMIndex <= 0) {
                    IssueSevereInputFieldError(RoutineName,
                                               ObjName_ZoneCoupled_Slab,
                                               DataIPShortCuts::cAlphaArgs(1),
                                               DataIPShortCuts::cAlphaFieldNames(4),
                                               DataIPShortCuts::cAlphaArgs(4),
                                               "Could not match with an Other Side Conditions Model input object.",
                                               ErrorsFound);
                    ErrorsFound = true;
                } else {
                    int const NumSurfacesWithThisOSCM = GetSurfaceCountForOSCM(thisDomain.ZoneCoupledOSCMIndex);
                    if (NumSurfacesWithThisOSCM <= 0) {
                        IssueSevereInputFieldError(
                                RoutineName,
                                ObjName_ZoneCoupled_Slab,
                                DataIPShortCuts::cAlphaArgs(1),
                                DataIPShortCuts::cAlphaFieldNames(4),
                                DataIPShortCuts::cAlphaArgs(4),
                                "Entry matched an Other Side Conditions Model, but no surfaces were found to be using this Other Side Conditions Model.",
                                ErrorsFound);
                        ErrorsFound = true;
                    } else {
                        thisDomain.ZoneCoupledSurfaces = GetSurfaceDataForOSCM(thisDomain.ZoneCoupledOSCMIndex);
                    }
                }

                // Total surface area
                auto lambda = [](Real64 total, ZoneCoupledSurfaceData const &z) { return total + z.SurfaceArea; };
                Real64 const ThisArea = std::accumulate(thisDomain.ZoneCoupledSurfaces.begin(),
                                                        thisDomain.ZoneCoupledSurfaces.end(), 0.0, lambda);

                thisDomain.SlabArea = ThisArea / 4; // We are only interested in 1/4 of total area due to symmetry

                // Surface dimensions
                thisDomain.SlabWidth = std::sqrt(ThisArea / gdd.AspectRatio);
                thisDomain.SlabLength = thisDomain.SlabWidth * gdd.AspectRatio;

                // Check horizontal insulation width so as to prevent overlapping insulation. VertInsThickness is used here since it is used for vertical
                // partition thickness.
                if (!thisDomain.FullHorizInsPresent && ThisArea > 0.0) {
                    if (2 * (thisDomain.HorizInsWidth + thisDomain.VertInsThickness) >
                        thisDomain.SlabWidth ||
                        2 * (thisDomain.HorizInsWidth + thisDomain.VertInsThickness) >
                        thisDomain.SlabLength) {
                        ShowContinueError(RoutineName + ": Perimeter insulation width is too large.");
                        ShowContinueError("This would cause overlapping insulation. Check inputs.");
                        ShowContinueError("Defaulting to full horizontal insulation.");
                        ShowContinueError("Found in: " + gdd.ObjName);
                        thisDomain.FullHorizInsPresent = true;
                    }
                }

                // Set ground domain dimensions
                thisDomain.Extents.xMax = gdd.PerimeterOffset + thisDomain.SlabWidth / 2;
                thisDomain.Extents.yMax = gdd.Depth;
                thisDomain.Extents.zMax =
                        gdd.PerimeterOffset + thisDomain.SlabLength / 2;

                // Get mesh parameters

                // Mesh inputs
                thisDomain.Mesh.X.thisMeshDistribution = MeshDistribution::SymmetricGeometric;
                thisDomain.Mesh.Y.thisMeshDistribution = MeshDistribution::SymmetricGeometric;
                thisDomain.Mesh.Z.thisMeshDistribution = MeshDistribution::SymmetricGeometric;

                Real64 MeshCoefficient = DataIPShortCuts::rNumericArgs(12);
                if (MeshCoefficient == 0.0) MeshCoefficient = 1.6;
                thisDomain.Mesh.X.GeometricSeriesCoefficient = MeshCoefficient;
                thisDomain.Mesh.Y.GeometricSeriesCoefficient = MeshCoefficient;
                thisDomain.Mesh.Z.GeometricSeriesCoefficient = MeshCoefficient;

                int MeshCount = static_cast<int>(DataIPShortCuts::rNumericArgs(13));
                if (MeshCount == 0.0) MeshCount = 6;
                thisDomain.Mesh.X.RegionMeshCount = MeshCount;
                thisDomain.Mesh.Y.RegionMeshCount = MeshCount;
                thisDomain.Mesh.Z.RegionMeshCount = MeshCount;

                // Soil properties
                thisDomain.GroundProperties.Conductivity = gdd.SoilConductivity;
                thisDomain.GroundProperties.Density = gdd.SoilDensity;
                thisDomain.GroundProperties.SpecificHeat = gdd.SoilSpecificHeat;

                // Moisture properties
                thisDomain.Moisture.Theta_liq = gdd.MoistureContent / 100.0;
                thisDomain.Moisture.Theta_sat = gdd.SaturationMoistureContent / 100.0;

                thisDomain.NumSlabCells =
                        thisDomain.Mesh.Y.RegionMeshCount; // Need to clean this out at some point

                // Farfield model
                thisDomain.Farfield.groundTempModel = GetGroundTempModelAndInit(DataIPShortCuts::cAlphaArgs(2),
                                                                                DataIPShortCuts::cAlphaArgs(3));

                // Other parameters
                thisDomain.SimControls.Convergence_CurrentToPrevIteration = 0.001;
                thisDomain.SimControls.MaxIterationsPerTS = 250;

                // additional evapotranspiration parameter, min/max validated by IP
                thisDomain.Moisture.GroundCoverCoefficient = gdd.EvapotranspirationCoeff;

                // setup output variables
                thisDomain.SetupZoneCoupledOutputVariables();

                // add it to the main vector
                // domains.push_back(thisDomain);
            }
        }

        void ReadBasementInputs(int const StartingDomainNumForBasement, int const NumBasements, bool &ErrorsFound) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       Summer 2014  Sushobhit Acharya to accommodate basement calculations
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            static std::string const RoutineName("ReadBasementInputs");

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            int NumAlphas;  // Number of Alphas for each GetObjectItem call
            int NumNumbers; // Number of Numbers for each GetObjectItem call
            int IOStatus;   // Used in GetObjectItem
            int CurIndex;

            struct GroundDomainData {
                // Members
                std::string ObjName;
                Real64 Depth;
                Real64 AspectRatio;
                Real64 PerimeterOffset;
                Real64 MinSurfTemp;
                int MonthOfMinSurfTemp;
                Real64 HorizInsWidth;
                Real64 VertInsDepth;
                std::string HorizInsMaterial;
                std::string VertInsMaterial;

                // Default Constructor
                GroundDomainData()
                        : Depth(0.0), AspectRatio(0.0), PerimeterOffset(0.0), MinSurfTemp(0.0), MonthOfMinSurfTemp(0),
                          HorizInsWidth(0.0), VertInsDepth(0.0) {
                }
            };

            // initialize these counters properly so they can be incremented within the DO loop
            int DomainNum = StartingDomainNumForBasement - 1;

            // For each domain, we need to process the inputs into a local array of derived type, then resolve each one, creating definitions for a zone
            // coupled domain. This way, the outer get input routines can handle it as though they were generalized routines

            for (int BasementCtr = 1; BasementCtr <= NumBasements; ++BasementCtr) {

                // Increment the domain counters here
                ++DomainNum;

                // Read all the inputs for this domain object
                inputProcessor->getObjectItem(ObjName_ZoneCoupled_Basement,
                                              BasementCtr,
                                              DataIPShortCuts::cAlphaArgs,
                                              NumAlphas,
                                              DataIPShortCuts::rNumericArgs,
                                              NumNumbers,
                                              IOStatus,
                                              DataIPShortCuts::lNumericFieldBlanks,
                                              DataIPShortCuts::lAlphaFieldBlanks,
                                              DataIPShortCuts::cAlphaFieldNames,
                                              DataIPShortCuts::cNumericFieldNames);

                GroundDomainData gdd;

                // Get the name, validate
                gdd.ObjName = DataIPShortCuts::cAlphaArgs(1);
                GlobalNames::VerifyUniqueInterObjectName(
                        GroundDomainUniqueNames, DataIPShortCuts::cAlphaArgs(1), ObjName_ZoneCoupled_Basement,
                        DataIPShortCuts::cAlphaFieldNames(1), ErrorsFound);

                // Read in the some of the inputs into the local type for clarity during transition
                gdd.Depth = DataIPShortCuts::rNumericArgs(1);
                gdd.AspectRatio = DataIPShortCuts::rNumericArgs(2);
                gdd.PerimeterOffset = DataIPShortCuts::rNumericArgs(3);
                gdd.HorizInsWidth = DataIPShortCuts::rNumericArgs(10);
                gdd.VertInsDepth = DataIPShortCuts::rNumericArgs(12);

                auto &thisDomain = domains[DomainNum - 1];

                // Other inputs
                thisDomain.Name = DataIPShortCuts::cAlphaArgs(1);

                // Soil properties, validated min/max by IP
                thisDomain.GroundProperties.Conductivity = DataIPShortCuts::rNumericArgs(4);
                thisDomain.GroundProperties.Density = DataIPShortCuts::rNumericArgs(5);
                thisDomain.GroundProperties.SpecificHeat = DataIPShortCuts::rNumericArgs(6);

                // Moisture properties, validated min/max by IP, and converted to a fraction for computation here
                thisDomain.Moisture.Theta_liq = DataIPShortCuts::rNumericArgs(7) / 100.0;
                thisDomain.Moisture.Theta_sat = DataIPShortCuts::rNumericArgs(8) / 100.0;

                // check if there are blank inputs related to the basement,
                if (DataIPShortCuts::lNumericFieldBlanks(11) || DataIPShortCuts::lAlphaFieldBlanks(5) ||
                    DataIPShortCuts::lAlphaFieldBlanks(10)) {
                    ShowSevereError("Erroneous basement inputs for " + ObjName_ZoneCoupled_Basement + '=' +
                                    DataIPShortCuts::cAlphaArgs(1));
                    ShowContinueError("At least one basement input was left blank.");
                    ErrorsFound = true;
                }

                // Basement zone depth
                CurIndex = 11;
                thisDomain.BasementZone.Depth = DataIPShortCuts::rNumericArgs(CurIndex);
                if (thisDomain.BasementZone.Depth >= gdd.Depth ||
                    thisDomain.BasementZone.Depth <= 0.0) {
                    ShowSevereError("Invalid " + DataIPShortCuts::cNumericFieldNames(CurIndex));
                    ShowContinueError("Found in: " + thisDomain.Name);
                    ErrorsFound = true;
                }

                // get boundary condition model names and indices --error check
                CurIndex = 4;
                thisDomain.BasementZone.FloorBoundaryOSCMName = DataIPShortCuts::cAlphaArgs(CurIndex);
                thisDomain.BasementZone.FloorBoundaryOSCMIndex =
                        UtilityRoutines::FindItemInList(thisDomain.BasementZone.FloorBoundaryOSCMName,
                                                        DataSurfaces::OSCM);
                if (thisDomain.BasementZone.FloorBoundaryOSCMIndex <= 0) {
                    IssueSevereInputFieldError(RoutineName,
                                               ObjName_ZoneCoupled_Basement,
                                               DataIPShortCuts::cAlphaArgs(1),
                                               DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                               DataIPShortCuts::cAlphaArgs(CurIndex),
                                               "Could not match with an Other Side Conditions Model input object.",
                                               ErrorsFound);
                } else {
                    auto const &floorIndexes = GetSurfaceIndecesForOSCM(thisDomain.BasementZone.FloorBoundaryOSCMIndex);
                    if (floorIndexes.empty()) {
                        IssueSevereInputFieldError(
                                RoutineName,
                                ObjName_ZoneCoupled_Basement,
                                DataIPShortCuts::cAlphaArgs(1),
                                DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                DataIPShortCuts::cAlphaArgs(CurIndex),
                                "Entry matched an Other Side Conditions Model, but no surfaces were found to be using this Other Side Conditions Model.",
                                ErrorsFound);
                    } else {
                        thisDomain.BasementZone.FloorSurfacePointers = floorIndexes;
                        // Create GetSurfaceDataForOSCM function
                        thisDomain.ZoneCoupledSurfaces = GetSurfaceDataForOSCM(
                                thisDomain.BasementZone.FloorBoundaryOSCMIndex);
                    }
                }

                CurIndex = 8;
                thisDomain.BasementZone.WallBoundaryOSCMName = DataIPShortCuts::cAlphaArgs(CurIndex);
                thisDomain.BasementZone.WallBoundaryOSCMIndex =
                        UtilityRoutines::FindItemInList(thisDomain.BasementZone.WallBoundaryOSCMName,
                                                        DataSurfaces::OSCM);
                if (thisDomain.BasementZone.WallBoundaryOSCMIndex <= 0) {
                    IssueSevereInputFieldError(RoutineName,
                                               ObjName_ZoneCoupled_Basement,
                                               DataIPShortCuts::cAlphaArgs(1),
                                               DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                               DataIPShortCuts::cAlphaArgs(CurIndex),
                                               "Could not match with an Other Side Conditions Model input object.",
                                               ErrorsFound);
                    ErrorsFound = true;
                } else {
                    auto const &wallIndexes = GetSurfaceIndecesForOSCM(thisDomain.BasementZone.WallBoundaryOSCMIndex);
                    if (wallIndexes.empty()) {
                        IssueSevereInputFieldError(
                                RoutineName,
                                ObjName_ZoneCoupled_Basement,
                                DataIPShortCuts::cAlphaArgs(1),
                                DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                DataIPShortCuts::cAlphaArgs(CurIndex),
                                "Entry matched an Other Side Conditions Model, but no surfaces were found to be using this Other Side Conditions Model.",
                                ErrorsFound);
                        ErrorsFound = true;
                    } else {
                        thisDomain.BasementZone.WallSurfacePointers = wallIndexes;
                    }
                }

                // get some convergence tolerances, minimum/maximum are enforced by the IP, along with default values if user left them blank
                thisDomain.SimControls.Convergence_CurrentToPrevIteration = 0.01;
                thisDomain.SimControls.MaxIterationsPerTS = 250;

                // additional evapotranspiration parameter, min/max validated by IP
                thisDomain.Moisture.GroundCoverCoefficient = DataIPShortCuts::rNumericArgs(9);

                // assign the mesh count
                int meshCount;
                if (DataIPShortCuts::lNumericFieldBlanks(13)) {
                    meshCount = 4;
                } else {
                    meshCount = static_cast<int>(DataIPShortCuts::rNumericArgs(13));
                }
                thisDomain.Mesh.X.RegionMeshCount = meshCount;
                thisDomain.Mesh.Y.RegionMeshCount = meshCount;
                thisDomain.Mesh.Z.RegionMeshCount = meshCount;

                thisDomain.Mesh.X.thisMeshDistribution = MeshDistribution::Uniform;
                thisDomain.Mesh.Y.thisMeshDistribution = MeshDistribution::Uniform;
                thisDomain.Mesh.Z.thisMeshDistribution = MeshDistribution::Uniform;

                // Initialize properties for basement interface cells
                thisDomain.BasementInterfaceProperties.Conductivity = 500.0;
                thisDomain.BasementInterfaceProperties.SpecificHeat = 1.0;
                thisDomain.BasementInterfaceProperties.Density = 1.0;

                // set flag for horizontal insulation
                // Check DataIPShortCuts::cAlphaArgs value
                if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(5), "NO")) {
                    thisDomain.HorizInsPresentFlag = false;
                } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(5), "YES")) {
                    thisDomain.HorizInsPresentFlag = true;
                } else {
                    ShowSevereError(
                            "Invalid " + DataIPShortCuts::cAlphaFieldNames(5) + "=" + DataIPShortCuts::cAlphaArgs(5));
                    ShowContinueError("Found in: " + thisDomain.Name);
                    ErrorsFound = true;
                }

                // Get horizontal insulation material properties
                if (thisDomain.HorizInsPresentFlag) {
                    gdd.HorizInsMaterial = DataIPShortCuts::cAlphaArgs(6);
                    thisDomain.HorizInsMaterialNum = UtilityRoutines::FindItemInList(DataIPShortCuts::cAlphaArgs(6),
                                                                                     DataHeatBalance::Material,
                                                                                     DataHeatBalance::TotMaterials);
                    if (thisDomain.HorizInsMaterialNum == 0) {
                        ShowSevereError("Invalid " + DataIPShortCuts::cAlphaFieldNames(6) + "=" +
                                        DataIPShortCuts::cAlphaArgs(6));
                        ShowContinueError("Found in: " + thisDomain.Name);
                        ErrorsFound = true;
                    } else {
                        thisDomain.HorizInsThickness = DataHeatBalance::Material(
                                thisDomain.HorizInsMaterialNum).Thickness;
                        thisDomain.HorizInsProperties.Density = DataHeatBalance::Material(
                                thisDomain.HorizInsMaterialNum).Density;
                        thisDomain.HorizInsProperties.SpecificHeat = DataHeatBalance::Material(
                                thisDomain.HorizInsMaterialNum).SpecHeat;
                        thisDomain.HorizInsProperties.Conductivity = DataHeatBalance::Material(
                                thisDomain.HorizInsMaterialNum).Conductivity;
                    }

                    // Set flag for horizontal insulation extents
                    if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(7), "PERIMETER")) {
                        thisDomain.FullHorizInsPresent = false;
                        // Horizontal insulation perimeter width
                        if (gdd.HorizInsWidth > 0.0) {
                            thisDomain.HorizInsWidth = gdd.HorizInsWidth;
                        } else {
                            ShowSevereError("Invalid " + DataIPShortCuts::cNumericFieldNames(10));
                            ShowContinueError("Found in: " + thisDomain.Name);
                            ErrorsFound = true;
                        }
                    } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(7), "FULL")) {
                        thisDomain.FullHorizInsPresent = true;
                    } else {
                        ShowSevereError("Invalid " + DataIPShortCuts::cAlphaFieldNames(7) + "=" +
                                        DataIPShortCuts::cAlphaArgs(7));
                        ShowContinueError("Found in: " + thisDomain.Name);
                        ErrorsFound = true;
                    }

                    // Horizontal insulation perimeter width
                    thisDomain.HorizInsWidth = gdd.HorizInsWidth;
                }

                // set flag for vertical insulation
                if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(9), "NO")) {
                    thisDomain.VertInsPresentFlag = false;
                } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(9), "YES")) {
                    thisDomain.VertInsPresentFlag = true;
                } else {
                    ShowSevereError(
                            "Invalid " + DataIPShortCuts::cAlphaFieldNames(9) + "=" + DataIPShortCuts::cAlphaArgs(9));
                    ShowContinueError("Found in: " + thisDomain.Name);
                    ErrorsFound = true;
                }

                // Get vertical insulation material properties
                if (thisDomain.VertInsPresentFlag) {
                    // Check if vertical insulation is in domain
                    if (gdd.VertInsDepth >= gdd.Depth || gdd.VertInsDepth <= 0.0) {
                        ShowSevereError("Invalid " + DataIPShortCuts::cNumericFieldNames(12));
                        ShowContinueError("Found in: " + thisDomain.Name);
                        ErrorsFound = true;
                    } else {
                        // Set insulation depth
                        thisDomain.VertInsDepth = gdd.VertInsDepth;
                    }

                    gdd.VertInsMaterial = DataIPShortCuts::cAlphaArgs(10);
                    thisDomain.VertInsMaterialNum = UtilityRoutines::FindItemInList(DataIPShortCuts::cAlphaArgs(10),
                                                                                    DataHeatBalance::Material,
                                                                                    DataHeatBalance::TotMaterials);
                    if (thisDomain.VertInsMaterialNum == 0) {
                        ShowSevereError("Invalid " + DataIPShortCuts::cAlphaFieldNames(10) + "=" +
                                        DataIPShortCuts::cAlphaArgs(10));
                        ShowContinueError("Found in: " + thisDomain.Name);
                        ErrorsFound = true;
                    } else {
                        thisDomain.VertInsThickness = DataHeatBalance::Material(
                                thisDomain.VertInsMaterialNum).Thickness;
                        thisDomain.VertInsProperties.Density = DataHeatBalance::Material(
                                thisDomain.VertInsMaterialNum).Density;
                        thisDomain.VertInsProperties.SpecificHeat = DataHeatBalance::Material(
                                thisDomain.VertInsMaterialNum).SpecHeat;
                        thisDomain.VertInsProperties.Conductivity = DataHeatBalance::Material(
                                thisDomain.VertInsMaterialNum).Conductivity;
                    }
                }

                // Set simulation interval flag
                if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(11), "TIMESTEP")) {
                    thisDomain.SimTimeStepFlag = true;
                } else if (UtilityRoutines::SameString(DataIPShortCuts::cAlphaArgs(11), "HOURLY")) {
                    thisDomain.SimHourlyFlag = true;
                } else {
                    ShowSevereError(
                            "Invalid " + DataIPShortCuts::cAlphaFieldNames(11) + "=" + DataIPShortCuts::cAlphaArgs(11));
                    ShowContinueError("Found in: " + thisDomain.Name);
                    ErrorsFound = true;
                }

                // Farfield ground temperature model
                thisDomain.Farfield.groundTempModel = GetGroundTempModelAndInit(DataIPShortCuts::cAlphaArgs(2),
                                                                                DataIPShortCuts::cAlphaArgs(3));

                // Domain perimeter offset
                thisDomain.PerimeterOffset = gdd.PerimeterOffset;

                // Total surface area
                Real64 ThisArea = 0.0;

                for (auto &z : thisDomain.ZoneCoupledSurfaces) {
                    ThisArea += z.SurfaceArea;
                }

                // Surface dimensions
                thisDomain.BasementZone.Width = sqrt(ThisArea / gdd.AspectRatio);
                thisDomain.BasementZone.Length = thisDomain.BasementZone.Width * gdd.AspectRatio;

                // Set ground domain dimensions
                // get width and length from aspect ratio later
                thisDomain.Extents.xMax = gdd.PerimeterOffset + thisDomain.BasementZone.Width / 2;
                thisDomain.Extents.yMax = gdd.Depth;
                thisDomain.Extents.zMax =
                        gdd.PerimeterOffset + thisDomain.BasementZone.Length / 2;

                // Check horizontal insulation width so as to prevent overlapping insulation. VertInsThickness is used here since it is used for vertical
                // partition thickness.
                if (!thisDomain.FullHorizInsPresent && ThisArea > 0.0) {
                    if ((thisDomain.HorizInsWidth + thisDomain.VertInsThickness) >
                        thisDomain.BasementZone.Width / 2.0 ||
                        (thisDomain.HorizInsWidth + thisDomain.VertInsThickness) >
                        thisDomain.BasementZone.Length / 2.0) {
                        ShowContinueError(RoutineName + ": Perimeter insulation width is too large.");
                        ShowContinueError("This would cause overlapping insulation. Check inputs.");
                        ShowContinueError("Defaulting to full horizontal insulation.");
                        ShowContinueError("Found in: " + thisDomain.Name);
                        thisDomain.FullHorizInsPresent = true;
                    }
                }

                //******* We'll first set up the domain ********
                thisDomain.IsActuallyPartOfAHorizontalTrench = false;
                thisDomain.HasAPipeCircuit = false;
                thisDomain.HasZoneCoupledSlab = false;
                thisDomain.HasBasement = false;
                thisDomain.HasZoneCoupledBasement = true;

                // Domain name
                thisDomain.Name = gdd.ObjName;

                // setup output variables
                thisDomain.SetupZoneCoupledOutputVariables();

                // add it to the main vector
                // domains.push_back(thisDomain);
            }
        }

        void ReadPipeCircuitInputs(int const NumPipeCircuits, bool &ErrorsFound) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            static std::string const RoutineName("ReadPipeCircuitInputs");

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            int NumAlphas;
            int NumNumbers;
            int IOStatus;
            int CurIndex;

            for (int PipeCircuitCounter = 1; PipeCircuitCounter <= NumPipeCircuits; ++PipeCircuitCounter) {

                // Read all the inputs for this pipe circuit
                inputProcessor->getObjectItem(ObjName_Circuit,
                                              PipeCircuitCounter,
                                              DataIPShortCuts::cAlphaArgs,
                                              NumAlphas,
                                              DataIPShortCuts::rNumericArgs,
                                              NumNumbers,
                                              IOStatus,
                                              DataIPShortCuts::lNumericFieldBlanks,
                                              DataIPShortCuts::lAlphaFieldBlanks,
                                              DataIPShortCuts::cAlphaFieldNames,
                                              DataIPShortCuts::cNumericFieldNames);

                auto &thisCircuit = circuits[PipeCircuitCounter - 1];

                // Get the name, validate
                thisCircuit.Name = DataIPShortCuts::cAlphaArgs(1);
                thisCircuit.CircuitIndex = PipeCircuitCounter - 1;
                UtilityRoutines::IsNameEmpty(DataIPShortCuts::cAlphaArgs(1), DataIPShortCuts::cCurrentModuleObject,
                                             ErrorsFound);

                // Read pipe thermal properties, validated by IP
                thisCircuit.PipeProperties.Conductivity = DataIPShortCuts::rNumericArgs(1);
                thisCircuit.PipeProperties.Density = DataIPShortCuts::rNumericArgs(2);
                thisCircuit.PipeProperties.SpecificHeat = DataIPShortCuts::rNumericArgs(3);

                // Read pipe sizing, validated individually by IP, validated comparison here
                thisCircuit.PipeSize.InnerDia = DataIPShortCuts::rNumericArgs(4);
                thisCircuit.PipeSize.OuterDia = DataIPShortCuts::rNumericArgs(5);
                if (thisCircuit.PipeSize.InnerDia >= thisCircuit.PipeSize.OuterDia) {
                    CurIndex = 5;
                    IssueSevereInputFieldError(RoutineName,
                                               ObjName_Circuit,
                                               DataIPShortCuts::cAlphaArgs(1),
                                               DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                               DataIPShortCuts::cAlphaArgs(CurIndex),
                                               "Outer diameter must be greater than inner diameter.",
                                               ErrorsFound);
                }

                // Read design flow rate, validated positive by IP
                thisCircuit.DesignVolumeFlowRate = DataIPShortCuts::rNumericArgs(6);

                // Read inlet and outlet node names and validate them
                thisCircuit.InletNodeName = DataIPShortCuts::cAlphaArgs(2);
                thisCircuit.InletNodeNum = NodeInputManager::GetOnlySingleNode(
                        DataIPShortCuts::cAlphaArgs(2), ErrorsFound, ObjName_Circuit, DataIPShortCuts::cAlphaArgs(1),
                        DataLoopNode::NodeType_Water, DataLoopNode::NodeConnectionType_Inlet, 1,
                        DataLoopNode::ObjectIsNotParent);
                if (thisCircuit.InletNodeNum == 0) {
                    CurIndex = 2;
                    IssueSevereInputFieldError(
                            RoutineName, ObjName_Circuit, DataIPShortCuts::cAlphaArgs(1),
                            DataIPShortCuts::cAlphaFieldNames(CurIndex), DataIPShortCuts::cAlphaArgs(CurIndex),
                            "Bad node name.", ErrorsFound);
                }
                thisCircuit.OutletNodeName = DataIPShortCuts::cAlphaArgs(3);
                thisCircuit.OutletNodeNum = NodeInputManager::GetOnlySingleNode(
                        DataIPShortCuts::cAlphaArgs(3), ErrorsFound, ObjName_Circuit, DataIPShortCuts::cAlphaArgs(1),
                        DataLoopNode::NodeType_Water, DataLoopNode::NodeConnectionType_Outlet, 1,
                        DataLoopNode::ObjectIsNotParent);
                if (thisCircuit.OutletNodeNum == 0) {
                    CurIndex = 3;
                    IssueSevereInputFieldError(
                            RoutineName, ObjName_Circuit, DataIPShortCuts::cAlphaArgs(1),
                            DataIPShortCuts::cAlphaFieldNames(CurIndex), DataIPShortCuts::cAlphaArgs(CurIndex),
                            "Bad node name.", ErrorsFound);
                }
                BranchNodeConnections::TestCompSet(ObjName_Circuit, DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cAlphaArgs(2), DataIPShortCuts::cAlphaArgs(3),
                                                   "Piping System Circuit Nodes");

                // Convergence tolerance values, validated by IP
                thisCircuit.Convergence_CurrentToPrevIteration = DataIPShortCuts::rNumericArgs(7);
                thisCircuit.MaxIterationsPerTS = static_cast<int>(DataIPShortCuts::rNumericArgs(8));

                // Radial mesh inputs, validated by IP
                // -- mesh thickness should be considered slightly dangerous until mesh dev engine can trap erroneous values
                thisCircuit.NumRadialCells = static_cast<int>(DataIPShortCuts::rNumericArgs(9));
                thisCircuit.RadialMeshThickness = DataIPShortCuts::rNumericArgs(10);

                // Read number of pipe segments for this circuit, allocate arrays
                int const NumPipeSegments = static_cast<int>(DataIPShortCuts::rNumericArgs(11));

                // Need to loop once to store the names ahead of time because calling the segment factory will override cAlphaArgs
                std::vector<std::string> segmentNamesToFind;
                int const NumAlphasBeforeSegmentOne = 3;
                for (int ThisCircuitPipeSegmentCounter = 1;
                     ThisCircuitPipeSegmentCounter <= NumPipeSegments; ++ThisCircuitPipeSegmentCounter) {
                    CurIndex = ThisCircuitPipeSegmentCounter + NumAlphasBeforeSegmentOne;
                    if (DataIPShortCuts::lAlphaFieldBlanks(CurIndex)) {
                        IssueSevereInputFieldError(RoutineName,
                                                   ObjName_Circuit,
                                                   DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                                   DataIPShortCuts::cAlphaArgs(CurIndex),
                                                   "Expected a pipe segment name, check pipe segment count input field.",
                                                   ErrorsFound);
                    }
                    segmentNamesToFind.push_back(DataIPShortCuts::cAlphaArgs(CurIndex));
                }
                // then we can loop through and allow the factory to be called and carry on
                for (auto &segmentNameToFind : segmentNamesToFind) {
                    thisCircuit.pipeSegments.push_back(Segment::factory(segmentNameToFind));
                }

            } // All pipe circuits in input
        }

        Segment *Segment::factory(std::string segmentName) {
            if (GetSegmentInputFlag) {
                bool errorsFound = false;
                ReadPipeSegmentInputs(errorsFound);
                GetSegmentInputFlag = false;
            }
            // Now look for this particular segment in the list
            for (auto &segment : segments) {
                if (segment.Name == segmentName) {
                    return &segment;
                }
            }
            // If we didn't find it, fatal
            ShowFatalError(
                    "PipeCircuitInfoFactory: Error getting inputs for circuit named: " + segmentName); // LCOV_EXCL_LINE
            // Shut up the compiler
            return nullptr; // LCOV_EXCL_LINE
        }

        void ReadPipeSegmentInputs(bool &ErrorsFound) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            static std::string const RoutineName("ReadPipeSegmentInputs");

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            int NumAlphas;  // Number of Alphas for each GetObjectItem call
            int NumNumbers; // Number of Numbers for each GetObjectItem call
            int IOStatus;   // Used in GetObjectItem
            int CurIndex;

            // Read in all pipe segments
            int NumPipeSegmentsInInput = inputProcessor->getNumObjectsFound(ObjName_Segment);
            for (int SegmentCtr = 1; SegmentCtr <= NumPipeSegmentsInInput; ++SegmentCtr) {

                // Read all inputs for this pipe segment
                inputProcessor->getObjectItem(ObjName_Segment,
                                              SegmentCtr,
                                              DataIPShortCuts::cAlphaArgs,
                                              NumAlphas,
                                              DataIPShortCuts::rNumericArgs,
                                              NumNumbers,
                                              IOStatus,
                                              DataIPShortCuts::lNumericFieldBlanks,
                                              DataIPShortCuts::lAlphaFieldBlanks,
                                              DataIPShortCuts::cAlphaFieldNames,
                                              DataIPShortCuts::cNumericFieldNames);

                Segment thisSegment;

                // Get the name, validate
                thisSegment.Name = DataIPShortCuts::cAlphaArgs(1);
                UtilityRoutines::IsNameEmpty(DataIPShortCuts::cAlphaArgs(1), DataIPShortCuts::cCurrentModuleObject,
                                             ErrorsFound);
                // Read in the pipe location, validated as positive by IP
                // -- note that these values will be altered by the main GetInput routine in two ways:
                //   1) shift for basement wall if selected
                //   2) invert y direction to be measured from domain bottom surface for calculations
                thisSegment.PipeLocation = PointF(DataIPShortCuts::rNumericArgs(1), DataIPShortCuts::rNumericArgs(2));

                // Read in the flow direction
                {
                    auto const SELECT_CASE_var(stripped(DataIPShortCuts::cAlphaArgs(2)));
                    if (SELECT_CASE_var == "INCREASINGZ") {
                        thisSegment.FlowDirection = SegmentFlow::IncreasingZ;
                    } else if (SELECT_CASE_var == "DECREASINGZ") {
                        thisSegment.FlowDirection = SegmentFlow::DecreasingZ;
                    } else {
                        CurIndex = 2;
                        IssueSevereInputFieldError(RoutineName,
                                                   ObjName_Segment,
                                                   DataIPShortCuts::cAlphaArgs(1),
                                                   DataIPShortCuts::cAlphaFieldNames(CurIndex),
                                                   DataIPShortCuts::cAlphaArgs(CurIndex),
                                                   "Invalid flow direction, use one of the available keys.",
                                                   ErrorsFound);
                    }
                }

                segments.push_back(thisSegment);
            }
        }

        void ReadHorizontalTrenchInputs(int const StartingDomainNumForHorizontal,
                                        int const StartingCircuitNumForHorizontal,
                                        bool &ErrorsFound) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   September 2012
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            static std::string const RoutineName("ReadHorizontalTrenchInputs");

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            int NumAlphas;  // Number of Alphas for each GetObjectItem call
            int NumNumbers; // Number of Numbers for each GetObjectItem call
            int IOStatus;   // Used in GetObjectItem
            int CurIndex;

            struct HorizontalTrenchData {
                // Members
                std::string ObjName;
                std::string InletNodeName;
                std::string OutletNodeName;
                Real64 AxialLength;
                Real64 PipeID;
                Real64 PipeOD;
                int NumPipes;
                Real64 BurialDepth;
                Real64 DesignFlowRate;
                Real64 SoilConductivity;
                Real64 SoilDensity;
                Real64 SoilSpecificHeat;
                Real64 PipeConductivity;
                Real64 PipeDensity;
                Real64 PipeSpecificHeat;
                Real64 InterPipeSpacing;
                Real64 MoistureContent;
                Real64 SaturationMoistureContent;
                Real64 EvapotranspirationCoeff;
                Real64 MinSurfTemp;
                int MonthOfMinSurfTemp;

                // Default Constructor
                HorizontalTrenchData()
                        : AxialLength(0.0), PipeID(0.0), PipeOD(0.0), NumPipes(0), BurialDepth(0.0),
                          DesignFlowRate(0.0), SoilConductivity(0.0),
                          SoilDensity(0.0), SoilSpecificHeat(0.0), PipeConductivity(0.0), PipeDensity(0.0),
                          PipeSpecificHeat(0.0), InterPipeSpacing(0.0),
                          MoistureContent(0.0), SaturationMoistureContent(0.0), EvapotranspirationCoeff(0.0),
                          MinSurfTemp(0.0), MonthOfMinSurfTemp(0) {
                }
            };

            // initialize these counters properly so they can be incremented within the DO loop
            int DomainCtr = StartingDomainNumForHorizontal - 1;
            int CircuitCtr = StartingCircuitNumForHorizontal - 1;

            // For each horizontal, we need to process the inputs into a local array of derived type,
            //  then resolve each one, creating definitions for a pipe domain, pipe circuit, and series of pipe segments
            // This way, the outer get input routines can handle it as though they were generalized routines

            int NumHorizontalTrenches = inputProcessor->getNumObjectsFound(ObjName_HorizTrench);

            // Read in all pipe segments
            for (int HorizontalGHXCtr = 1; HorizontalGHXCtr <= NumHorizontalTrenches; ++HorizontalGHXCtr) {

                // Increment the domain and circuit counters here
                ++DomainCtr;
                ++CircuitCtr;

                // Read all inputs for this pipe segment
                inputProcessor->getObjectItem(ObjName_HorizTrench,
                                              HorizontalGHXCtr,
                                              DataIPShortCuts::cAlphaArgs,
                                              NumAlphas,
                                              DataIPShortCuts::rNumericArgs,
                                              NumNumbers,
                                              IOStatus,
                                              DataIPShortCuts::lNumericFieldBlanks,
                                              DataIPShortCuts::lAlphaFieldBlanks,
                                              DataIPShortCuts::cAlphaFieldNames,
                                              DataIPShortCuts::cNumericFieldNames);

                HorizontalTrenchData htd;

                // Get the name, validate
                htd.ObjName = DataIPShortCuts::cAlphaArgs(1);
                UtilityRoutines::IsNameEmpty(DataIPShortCuts::cAlphaArgs(1), DataIPShortCuts::cCurrentModuleObject,
                                             ErrorsFound);

                // Read in the rest of the inputs into the local type for clarity during transition
                htd.InletNodeName = DataIPShortCuts::cAlphaArgs(2);
                htd.OutletNodeName = DataIPShortCuts::cAlphaArgs(3);
                htd.DesignFlowRate = DataIPShortCuts::rNumericArgs(1);
                htd.AxialLength = DataIPShortCuts::rNumericArgs(2);
                htd.NumPipes = static_cast<int>(DataIPShortCuts::rNumericArgs(3));
                htd.InterPipeSpacing = DataIPShortCuts::rNumericArgs(4);
                htd.PipeID = DataIPShortCuts::rNumericArgs(5);
                htd.PipeOD = DataIPShortCuts::rNumericArgs(6);
                htd.BurialDepth = DataIPShortCuts::rNumericArgs(7);
                htd.SoilConductivity = DataIPShortCuts::rNumericArgs(8);
                htd.SoilDensity = DataIPShortCuts::rNumericArgs(9);
                htd.SoilSpecificHeat = DataIPShortCuts::rNumericArgs(10);
                htd.PipeConductivity = DataIPShortCuts::rNumericArgs(11);
                htd.PipeDensity = DataIPShortCuts::rNumericArgs(12);
                htd.PipeSpecificHeat = DataIPShortCuts::rNumericArgs(13);
                htd.MoistureContent = DataIPShortCuts::rNumericArgs(14);
                htd.SaturationMoistureContent = DataIPShortCuts::rNumericArgs(15);
                htd.EvapotranspirationCoeff = DataIPShortCuts::rNumericArgs(16);

                auto &thisDomain = domains[DomainCtr - 1];

                //******* We'll first set up the domain ********
                // the extents will be: zMax = axial length; yMax = burial depth*2; xMax = ( NumPipes+1 )*HorizontalPipeSpacing
                thisDomain.IsActuallyPartOfAHorizontalTrench = true;
                gio::write(thisDomain.Name, "( 'HorizontalTrenchDomain',I4 )") << HorizontalGHXCtr;
                thisDomain.Extents.xMax = (double(htd.NumPipes) + 1.0) * htd.InterPipeSpacing;
                thisDomain.Extents.yMax = 2.0 * htd.BurialDepth;
                thisDomain.Extents.zMax = htd.AxialLength;

                // set up the mesh with some default parameters
                thisDomain.Mesh.X.RegionMeshCount = 4;
                thisDomain.Mesh.X.thisMeshDistribution = MeshDistribution::Uniform;
                thisDomain.Mesh.Y.RegionMeshCount = 4;
                thisDomain.Mesh.Y.thisMeshDistribution = MeshDistribution::Uniform;
                thisDomain.Mesh.Z.RegionMeshCount = 4;
                thisDomain.Mesh.Z.thisMeshDistribution = MeshDistribution::Uniform;

                // Soil properties
                thisDomain.GroundProperties.Conductivity = htd.SoilConductivity;
                thisDomain.GroundProperties.Density = htd.SoilDensity;
                thisDomain.GroundProperties.SpecificHeat = htd.SoilSpecificHeat;

                // Moisture properties
                thisDomain.Moisture.Theta_liq = htd.MoistureContent / 100.0;
                thisDomain.Moisture.Theta_sat = htd.SaturationMoistureContent / 100.0;

                // Farfield model parameters
                thisDomain.Farfield.groundTempModel = GetGroundTempModelAndInit(DataIPShortCuts::cAlphaArgs(4),
                                                                                DataIPShortCuts::cAlphaArgs(5));

                // Other parameters
                thisDomain.SimControls.Convergence_CurrentToPrevIteration = 0.001;
                thisDomain.SimControls.MaxIterationsPerTS = 250;

                // additional evapotranspiration parameter, min/max validated by IP
                thisDomain.Moisture.GroundCoverCoefficient = htd.EvapotranspirationCoeff;

                // Allocate the circuit placeholder arrays
                thisDomain.CircuitNames.push_back(htd.ObjName);

                // add it to the main vector
                // domains.push_back(thisDomain);

                //******* We'll next set up the circuit ********
                auto &thisCircuit = circuits[CircuitCtr - 1];
                thisCircuit.IsActuallyPartOfAHorizontalTrench = true;
                thisCircuit.Name = htd.ObjName;
                thisCircuit.CircuitIndex = CircuitCtr - 1;

                // Read pipe thermal properties
                thisCircuit.PipeProperties.Conductivity = htd.PipeConductivity;
                thisCircuit.PipeProperties.Density = htd.PipeDensity;
                thisCircuit.PipeProperties.SpecificHeat = htd.PipeSpecificHeat;

                // Pipe sizing
                thisCircuit.PipeSize.InnerDia = htd.PipeID;
                thisCircuit.PipeSize.OuterDia = htd.PipeOD;
                if (thisCircuit.PipeSize.InnerDia >= thisCircuit.PipeSize.OuterDia) {
                    // CurIndex = 5
                    // CALL IssueSevereInputFieldError( RoutineName, ObjName_Circuit, DataIPShortCuts::cAlphaArgs( 1 ), cAlphaFieldNames( CurIndex ), &
                    //                            DataIPShortCuts::cAlphaArgs( CurIndex ), 'Outer diameter must be greater than inner diameter.', ErrorsFound )
                }

                // Read design flow rate, validated positive by IP
                thisCircuit.DesignVolumeFlowRate = htd.DesignFlowRate;

                // Read inlet and outlet node names and validate them
                thisCircuit.InletNodeName = htd.InletNodeName;
                thisCircuit.InletNodeNum = NodeInputManager::GetOnlySingleNode(thisCircuit.InletNodeName,
                                                                               ErrorsFound,
                                                                               ObjName_HorizTrench,
                                                                               htd.ObjName,
                                                                               DataLoopNode::NodeType_Water,
                                                                               DataLoopNode::NodeConnectionType_Inlet,
                                                                               1,
                                                                               DataLoopNode::ObjectIsNotParent);
                if (thisCircuit.InletNodeNum == 0) {
                    CurIndex = 2;
                    // CALL IssueSevereInputFieldError( RoutineName, ObjName_Circuit, DataIPShortCuts::cAlphaArgs( 1 ), cAlphaFieldNames( CurIndex ), &
                    //                                DataIPShortCuts::cAlphaArgs( CurIndex ), 'Bad node name.', ErrorsFound )
                }
                thisCircuit.OutletNodeName = htd.OutletNodeName;
                thisCircuit.OutletNodeNum = NodeInputManager::GetOnlySingleNode(thisCircuit.OutletNodeName,
                                                                                ErrorsFound,
                                                                                ObjName_HorizTrench,
                                                                                htd.ObjName,
                                                                                DataLoopNode::NodeType_Water,
                                                                                DataLoopNode::NodeConnectionType_Outlet,
                                                                                1,
                                                                                DataLoopNode::ObjectIsNotParent);
                if (thisCircuit.OutletNodeNum == 0) {
                    CurIndex = 3;
                    // CALL IssueSevereInputFieldError( RoutineName, ObjName_Circuit, DataIPShortCuts::cAlphaArgs( 1 ), cAlphaFieldNames( CurIndex ), &
                    //                                DataIPShortCuts::cAlphaArgs( CurIndex ), 'Bad node name.', ErrorsFound )
                }
                BranchNodeConnections::TestCompSet(ObjName_HorizTrench,
                                                   htd.ObjName,
                                                   thisCircuit.InletNodeName,
                                                   thisCircuit.OutletNodeName,
                                                   "Piping System Circuit Nodes");

                // Convergence tolerance values, validated by IP
                thisCircuit.Convergence_CurrentToPrevIteration = 0.001;
                thisCircuit.MaxIterationsPerTS = 100;

                // Radial mesh inputs, validated by IP
                // -- mesh thickness should be considered slightly dangerous until mesh dev engine can trap erroneous values
                thisCircuit.NumRadialCells = 4;
                thisCircuit.RadialMeshThickness = thisCircuit.PipeSize.InnerDia / 2.0;

                // Read number of pipe segments for this circuit, allocate arrays
                int const NumPipeSegments = htd.NumPipes;

                //******* Then we'll do the segments *******!
                for (int ThisCircuitPipeSegmentCounter = 1;
                     ThisCircuitPipeSegmentCounter <= NumPipeSegments; ++ThisCircuitPipeSegmentCounter) {
                    Segment segment;
                    segment.Name = "HorizontalTrenchCircuit" + std::to_string(HorizontalGHXCtr) + "Segment" +
                                   std::to_string(ThisCircuitPipeSegmentCounter);
                    segment.IsActuallyPartOfAHorizontalTrench = true;
                    segment.PipeLocation = PointF(ThisCircuitPipeSegmentCounter * htd.InterPipeSpacing,
                                                  htd.BurialDepth);

                    if (mod(ThisCircuitPipeSegmentCounter, 2) != 0) {
                        segment.FlowDirection = SegmentFlow::IncreasingZ;
                    } else {
                        segment.FlowDirection = SegmentFlow::DecreasingZ;
                    }
                    // add it to the main segment array so it has a place to live
                    segments.push_back(segment);
                }

                // now that they are in the main vector, add them here
                int const newSizeSegmentVector = static_cast<int>(segments.size());
                for (int segmentIndexToGrab = newSizeSegmentVector - NumPipeSegments;
                     segmentIndexToGrab < newSizeSegmentVector; ++segmentIndexToGrab) {
                    thisCircuit.pipeSegments.push_back(&segments[segmentIndexToGrab]);
                }
            }
        }

        void SetupPipingSystemOutputVariables() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   September 2012
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            for (auto &thisSegment : segments) {

                if (!thisSegment.IsActuallyPartOfAHorizontalTrench) {

                    SetupOutputVariable("Pipe Segment Inlet Temperature",
                                        OutputProcessor::Unit::C,
                                        thisSegment.InletTemperature,
                                        "Plant",
                                        "Average",
                                        thisSegment.Name);
                    SetupOutputVariable("Pipe Segment Outlet Temperature",
                                        OutputProcessor::Unit::C,
                                        thisSegment.OutletTemperature,
                                        "Plant",
                                        "Average",
                                        thisSegment.Name);

                    SetupOutputVariable("Pipe Segment Fluid Heat Transfer Rate",
                                        OutputProcessor::Unit::W,
                                        thisSegment.FluidHeatLoss,
                                        "Plant",
                                        "Average",
                                        thisSegment.Name);
                }
            }

            for (auto &thisCircuit : circuits) {

                if (!thisCircuit.IsActuallyPartOfAHorizontalTrench) {

                    SetupOutputVariable("Pipe Circuit Mass Flow Rate",
                                        OutputProcessor::Unit::kg_s,
                                        thisCircuit.CurCircuitFlowRate,
                                        "Plant",
                                        "Average",
                                        thisCircuit.Name);

                    SetupOutputVariable("Pipe Circuit Inlet Temperature",
                                        OutputProcessor::Unit::C,
                                        thisCircuit.InletTemperature,
                                        "Plant",
                                        "Average",
                                        thisCircuit.Name);
                    SetupOutputVariable("Pipe Circuit Outlet Temperature",
                                        OutputProcessor::Unit::C,
                                        thisCircuit.OutletTemperature,
                                        "Plant",
                                        "Average",
                                        thisCircuit.Name);

                    SetupOutputVariable("Pipe Circuit Fluid Heat Transfer Rate",
                                        OutputProcessor::Unit::W,
                                        thisCircuit.FluidHeatLoss,
                                        "Plant",
                                        "Average",
                                        thisCircuit.Name);

                } else { // it is a horizontal trench

                    SetupOutputVariable("Ground Heat Exchanger Mass Flow Rate",
                                        OutputProcessor::Unit::kg_s,
                                        thisCircuit.CurCircuitFlowRate,
                                        "Plant",
                                        "Average",
                                        thisCircuit.Name);

                    SetupOutputVariable("Ground Heat Exchanger Inlet Temperature",
                                        OutputProcessor::Unit::C,
                                        thisCircuit.InletTemperature,
                                        "Plant",
                                        "Average",
                                        thisCircuit.Name);
                    SetupOutputVariable("Ground Heat Exchanger Outlet Temperature",
                                        OutputProcessor::Unit::C,
                                        thisCircuit.OutletTemperature,
                                        "Plant",
                                        "Average",
                                        thisCircuit.Name);

                    SetupOutputVariable("Ground Heat Exchanger Fluid Heat Transfer Rate",
                                        OutputProcessor::Unit::W,
                                        thisCircuit.FluidHeatLoss,
                                        "Plant",
                                        "Average",
                                        thisCircuit.Name);
                }
            }
        }

        void Domain::SetupZoneCoupledOutputVariables() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Matt Mitchell
            //       DATE WRITTEN   August 2014
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            if (this->HasZoneCoupledSlab) {
                // Zone-coupled slab outputs
                SetupOutputVariable("GroundDomain Slab Zone Coupled Surface Heat Flux",
                                    OutputProcessor::Unit::W_m2,
                                    this->HeatFlux,
                                    "Zone",
                                    "Average",
                                    this->Name);
                SetupOutputVariable("GroundDomain Slab Zone Coupled Surface Temperature",
                                    OutputProcessor::Unit::C,
                                    this->ZoneCoupledSurfaceTemp,
                                    "Zone",
                                    "Average",
                                    this->Name);
            } else if (this->HasZoneCoupledBasement) {
                // Zone-coupled basement wall outputs
                SetupOutputVariable("GroundDomain Basement Wall Interface Heat Flux",
                                    OutputProcessor::Unit::W_m2,
                                    this->WallHeatFlux,
                                    "Zone",
                                    "Average",
                                    this->Name);
                SetupOutputVariable("GroundDomain Basement Wall Interface Temperature",
                                    OutputProcessor::Unit::C,
                                    this->BasementWallTemp,
                                    "Zone",
                                    "Average",
                                    this->Name);
                // Zone-coupled basement floor outputs
                SetupOutputVariable("GroundDomain Basement Floor Interface Heat Flux",
                                    OutputProcessor::Unit::W_m2,
                                    this->FloorHeatFlux,
                                    "Zone",
                                    "Average",
                                    this->Name);
                SetupOutputVariable("GroundDomain Basement Floor Interface Temperature",
                                    OutputProcessor::Unit::C,
                                    this->BasementFloorTemp,
                                    "Zone",
                                    "Average",
                                    this->Name);
            }
        }

        void Domain::InitPipingSystems(int const CircuitNum) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            static std::string const RoutineName("InitPipingSystems");

            auto &thisCircuit = circuits[CircuitNum];

            // Do any one-time initializations
            if (thisCircuit.NeedToFindOnPlantLoop) {

                int TypeToLookFor;
                if (thisCircuit.IsActuallyPartOfAHorizontalTrench) {
                    TypeToLookFor = DataPlant::TypeOf_GrndHtExchgHorizTrench;
                } else {
                    TypeToLookFor = DataPlant::TypeOf_PipingSystemPipeCircuit;
                }

                bool errFlag = false;
                PlantUtilities::ScanPlantLoopsForObject(thisCircuit.Name,
                                                        TypeToLookFor,
                                                        thisCircuit.LoopNum,
                                                        thisCircuit.LoopSideNum,
                                                        thisCircuit.BranchNum,
                                                        thisCircuit.CompNum,
                                                        errFlag,
                                                        _,
                                                        _,
                                                        _,
                                                        _,
                                                        _);
                if (errFlag) {
                    ShowFatalError(
                            "PipingSystems:" + RoutineName + ": Program terminated due to previous condition(s).");
                }

                // Once we find ourselves on the plant loop, we can do other things
                Real64 rho = FluidProperties::GetDensityGlycol(DataPlant::PlantLoop(thisCircuit.LoopNum).FluidName,
                                                               DataGlobals::InitConvTemp,
                                                               DataPlant::PlantLoop(thisCircuit.LoopNum).FluidIndex,
                                                               RoutineName);
                thisCircuit.DesignMassFlowRate = thisCircuit.DesignVolumeFlowRate * rho;
                thisCircuit.NeedToFindOnPlantLoop = false;
            }

            if (this->DomainNeedsToBeMeshed) {

                this->developMesh();

                // would be OK to do some post-mesh error handling here I think
                for (auto &circuitIndex : this->CircuitIndices) {
                    for (auto &segment : circuits[circuitIndex].pipeSegments) {
                        if (!segment->PipeCellCoordinatesSet) {
                            ShowSevereError("PipingSystems:" + RoutineName + ":Pipe segment index not set.");
                            ShowContinueError("...Possibly because pipe segment was placed outside of the domain.");
                            ShowContinueError("...Verify piping system domain inputs, circuits, and segments.");
                            ShowFatalError("Preceding error causes program termination");
                        }
                    }
                }

                this->DomainNeedsToBeMeshed = false;
            }

            // The time init should be done here before we DoOneTimeInits because the DoOneTimeInits
            // includes a ground temperature initialization, which is based on the Cur%CurSimTimeSeconds variable
            // which would be carried over from the previous environment
            this->Cur.CurSimTimeStepSize = DataHVACGlobals::TimeStepSys * DataGlobals::SecInHour;
            this->Cur.CurSimTimeSeconds = (DataGlobals::DayOfSim - 1) * 24 + (DataGlobals::HourOfDay - 1) +
                                          (DataGlobals::TimeStep - 1) * DataGlobals::TimeStepZone +
                                          DataHVACGlobals::SysTimeElapsed;

            // There are also some inits that are "close to one time" inits...(one-time in standalone, each envrn in E+)
            if ((DataGlobals::BeginSimFlag && this->BeginSimInit) ||
                (DataGlobals::BeginEnvrnFlag && this->BeginSimEnvironment)) {

                // this seemed to clean up a lot of reverse DD stuff because fluid thermal properties were
                // being based on the inlet temperature, which wasn't updated until later
                thisCircuit.CurCircuitInletTemp = DataLoopNode::Node(thisCircuit.InletNodeNum).Temp;
                thisCircuit.InletTemperature = thisCircuit.CurCircuitInletTemp;

                this->DoOneTimeInitializations(CircuitNum);

                this->BeginSimInit = false;
                this->BeginSimEnvironment = false;
            }
            if (!DataGlobals::BeginSimFlag) this->BeginSimInit = true;
            if (!DataGlobals::BeginEnvrnFlag) this->BeginSimEnvironment = true;

            // Shift history arrays only if necessary
            if (std::abs(this->Cur.CurSimTimeSeconds - this->Cur.PrevSimTimeSeconds) > 1.0e-6) {
                this->Cur.PrevSimTimeSeconds = this->Cur.CurSimTimeSeconds;
                this->ShiftTemperaturesForNewTimeStep();
                this->DomainNeedsSimulation = true;
            }

            // Get the mass flow and inlet temperature to use for this time step
            int InletNodeNum = thisCircuit.InletNodeNum;
            int OutletNodeNum = thisCircuit.OutletNodeNum;
            thisCircuit.CurCircuitInletTemp = DataLoopNode::Node(InletNodeNum).Temp;

            // request design, set component flow will decide what to give us based on restrictions and flow lock status
            thisCircuit.CurCircuitFlowRate = thisCircuit.DesignMassFlowRate;
            PlantUtilities::SetComponentFlowRate(thisCircuit.CurCircuitFlowRate,
                                                 InletNodeNum,
                                                 OutletNodeNum,
                                                 thisCircuit.LoopNum,
                                                 thisCircuit.LoopSideNum,
                                                 thisCircuit.BranchNum,
                                                 thisCircuit.CompNum);
        }

        void Domain::UpdatePipingSystems(int const CircuitNum) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            int OutletNodeNum = circuits[CircuitNum].OutletNodeNum;
            auto const &out_cell(circuits[CircuitNum].CircuitOutletCell);
            DataLoopNode::Node(OutletNodeNum).Temp = this->Cells(out_cell.X, out_cell.Y,
                                                                 out_cell.Z).PipeCellData.Fluid.Temperature;
        }

        void IssueSevereInputFieldError(std::string const &RoutineName,
                                        std::string const &ObjectName,
                                        std::string const &InstanceName,
                                        std::string const &FieldName,
                                        std::string const &FieldEntry,
                                        std::string const &Condition,
                                        bool &ErrorsFound) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            ShowSevereError(RoutineName + ':' + ObjectName + "=\"" + InstanceName + "\", invalid " + FieldName + "=\"" +
                            FieldEntry +
                            "\", Condition: " + Condition);
            ErrorsFound = true;
        }

        void IssueSevereInputFieldError(std::string const &RoutineName,
                                        std::string const &ObjectName,
                                        std::string const &InstanceName,
                                        std::string const &FieldName,
                                        Real64 const FieldEntry,
                                        std::string const &Condition,
                                        bool &ErrorsFound) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            ShowSevereError(RoutineName + ':' + ObjectName + "=\"" + InstanceName + "\", invalid " + FieldName + "=\"" +
                            General::TrimSigDigits(FieldEntry, 3) + "\", Condition: " + Condition);
            ErrorsFound = true;
        }

        int GetSurfaceCountForOSCM(int const OSCMIndex) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            int RetVal = 0;
            for (int SurfCtr = 1; SurfCtr <= isize(DataSurfaces::Surface); ++SurfCtr) {
                if (DataSurfaces::Surface(SurfCtr).OSCMPtr == OSCMIndex) ++RetVal;
            }
            return RetVal;
        }

        std::vector<int> GetSurfaceIndecesForOSCM(int const OSCMIndex) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            std::vector<int> retVal;
            for (int SurfCtr = 1; SurfCtr <= isize(DataSurfaces::Surface); ++SurfCtr) {
                if (DataSurfaces::Surface(SurfCtr).OSCMPtr == OSCMIndex) {
                    retVal.push_back(SurfCtr);
                }
            }
            return retVal;
        }

        std::vector<ZoneCoupledSurfaceData> GetSurfaceDataForOSCM(int const OSCMIndex) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            std::vector<ZoneCoupledSurfaceData> RetVal;
            for (int SurfCtr = 1; SurfCtr <= isize(DataSurfaces::Surface); ++SurfCtr) {
                if (DataSurfaces::Surface(SurfCtr).OSCMPtr == OSCMIndex) {
                    ZoneCoupledSurfaceData z;
                    z.IndexInSurfaceArray = SurfCtr;
                    z.SurfaceArea = DataSurfaces::Surface(SurfCtr).Area;
                    z.Zone = DataSurfaces::Surface(SurfCtr).Zone;
                    RetVal.push_back(z);
                }
            }
            return RetVal;
        }

        void Segment::initPipeCells(int const x, int const y) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            this->PipeCellCoordinates.X = x;
            this->PipeCellCoordinates.Y = y;
            this->PipeCellCoordinatesSet = true;
        }

        void Circuit::initInOutCells(CartesianCell const &in, CartesianCell const &out) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            this->CircuitInletCell = Point3DInteger(in.X_index, in.Y_index, in.Z_index);
            this->CircuitOutletCell = Point3DInteger(out.X_index, out.Y_index, out.Z_index);
        }

        bool Domain::IsConverged_CurrentToPrevIteration() {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            Real64 LocalMax(0.0);
            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto const &cell(this->Cells(X, Y, Z));
                        LocalMax = max(LocalMax, std::abs(cell.Temperature - cell.Temperature_PrevIteration));
                    }
                }
            }
            return (LocalMax < this->SimControls.Convergence_CurrentToPrevIteration);
        }

        bool IsConverged_PipeCurrentToPrevIteration(int const CircuitNum, CartesianCell const &CellToCheck) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            Real64 ThisCellMax;

            Real64 MaxDivAmount = 0.0;
            for (auto &radCell : CellToCheck.PipeCellData.Soil) {
                ThisCellMax = std::abs(radCell.Temperature - radCell.Temperature_PrevIteration);
                if (ThisCellMax > MaxDivAmount) {
                    MaxDivAmount = ThisCellMax;
                }
            }
            //'also do the pipe cell
            ThisCellMax = std::abs(CellToCheck.PipeCellData.Pipe.Temperature -
                                   CellToCheck.PipeCellData.Pipe.Temperature_PrevIteration);
            if (ThisCellMax > MaxDivAmount) {
                MaxDivAmount = ThisCellMax;
            }
            //'also do the water cell
            ThisCellMax = std::abs(CellToCheck.PipeCellData.Fluid.Temperature -
                                   CellToCheck.PipeCellData.Fluid.Temperature_PrevIteration);
            if (ThisCellMax > MaxDivAmount) {
                MaxDivAmount = ThisCellMax;
            }
            //'also do insulation if it exists
            if (circuits[CircuitNum].HasInsulation) {
                ThisCellMax = std::abs(CellToCheck.PipeCellData.Insulation.Temperature -
                                       CellToCheck.PipeCellData.Insulation.Temperature_PrevIteration);
                if (ThisCellMax > MaxDivAmount) {
                    MaxDivAmount = ThisCellMax;
                }
            }

            return (MaxDivAmount < circuits[CircuitNum].Convergence_CurrentToPrevIteration);
        }

        void Domain::ShiftTemperaturesForNewTimeStep() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto &cell(this->Cells(X, Y, Z));

                        cell.Temperature_PrevTimeStep = cell.Temperature;

                        if (cell.cellType == CellType::Pipe) {

                            for (auto &radCell : cell.PipeCellData.Soil) {
                                radCell.Temperature_PrevTimeStep = radCell.Temperature;
                            }

                            cell.PipeCellData.Fluid.Temperature_PrevTimeStep = cell.PipeCellData.Fluid.Temperature;

                            cell.PipeCellData.Pipe.Temperature_PrevTimeStep = cell.PipeCellData.Pipe.Temperature;

                            cell.PipeCellData.Insulation.Temperature_PrevTimeStep = cell.PipeCellData.Insulation.Temperature;
                        }
                    }
                }
            }
        }

        void Domain::ShiftTemperaturesForNewIteration() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto &cell(this->Cells(X, Y, Z));

                        cell.Temperature_PrevIteration = cell.Temperature;

                        if (cell.cellType == CellType::Pipe) {

                            for (auto &radCell : cell.PipeCellData.Soil) {
                                radCell.Temperature_PrevIteration = radCell.Temperature;
                            }

                            cell.PipeCellData.Fluid.Temperature_PrevIteration = cell.PipeCellData.Fluid.Temperature;

                            cell.PipeCellData.Pipe.Temperature_PrevIteration = cell.PipeCellData.Pipe.Temperature;

                            cell.PipeCellData.Insulation.Temperature_PrevIteration = cell.PipeCellData.Insulation.Temperature;
                        }
                    }
                }
            }
        }

        void ShiftPipeTemperaturesForNewIteration(CartesianCell &ThisPipeCell) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            if (ThisPipeCell.cellType == CellType::Pipe) { // It better be!

                for (auto &radCell : ThisPipeCell.PipeCellData.Soil) {
                    radCell.Temperature_PrevIteration = radCell.Temperature;
                }

                ThisPipeCell.PipeCellData.Fluid.Temperature_PrevIteration = ThisPipeCell.PipeCellData.Fluid.Temperature;

                ThisPipeCell.PipeCellData.Pipe.Temperature_PrevIteration = ThisPipeCell.PipeCellData.Pipe.Temperature;

                ThisPipeCell.PipeCellData.Insulation.Temperature_PrevIteration = ThisPipeCell.PipeCellData.Insulation.Temperature;
            }
        }

        bool Domain::CheckForOutOfRangeTemps() {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            Real64 const MaxLimit = this->SimControls.MaximumTemperatureLimit;
            Real64 const MinLimit = this->SimControls.MinimumTemperatureLimit;

            auto const &Cells(this->Cells);
            for (std::size_t i = 0, e = Cells.size(); i < e; ++i) {
                double const Temperature(Cells[i].Temperature);
                if ((Temperature > MaxLimit) || (Temperature < MinLimit)) return true;
            }
            return false;
        }

        Real64 CartesianCell::normalArea(Direction const direction) const {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            switch (direction) {
                case Direction::PositiveY:
                    return this->YNormalArea();
                case Direction::NegativeY:
                    return this->YNormalArea();
                case Direction::PositiveX:
                    return this->XNormalArea();
                case Direction::NegativeX:
                    return this->XNormalArea();
                case Direction::PositiveZ:
                    return this->ZNormalArea();
                case Direction::NegativeZ:
                    return this->ZNormalArea();
                default:
                    assert(false);
            }

            return 0;
        }

        void CartesianPipeCellInformation::ctor(CartesianPipeCellInformation &c,
                                                Real64 const GridCellWidth,
                                                RadialSizing const &PipeSizes,
                                                int const NumRadialNodes,
                                                Real64 const CellDepth,
                                                Real64 const InsulationThickness,
                                                Real64 const RadialGridExtent,
                                                bool const SimHasInsulation) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 InsulationInnerRadius;
            Real64 InsulationOuterRadius;
            Real64 InsulationCentroid;
            Real64 MinimumSoilRadius;

            //'calculate pipe radius
            Real64 const PipeOuterRadius = PipeSizes.OuterDia / 2.0;
            Real64 const PipeInnerRadius = PipeSizes.InnerDia / 2.0;

            //'--we will work from inside out, calculating dimensions and instantiating variables--
            //'first instantiate the water cell
            c.Fluid = FluidCellInformation(PipeInnerRadius, CellDepth);

            //'then the pipe cell
            c.Pipe = RadialCellInformation((PipeOuterRadius + PipeInnerRadius) / 2.0, PipeInnerRadius, PipeOuterRadius);

            //'then the insulation if we have it
            if (InsulationThickness > 0.0) {
                InsulationInnerRadius = PipeOuterRadius;
                InsulationOuterRadius = InsulationInnerRadius + InsulationThickness;
                InsulationCentroid = (InsulationInnerRadius + InsulationOuterRadius) / 2.0;
                c.Insulation = RadialCellInformation(InsulationCentroid, InsulationInnerRadius, InsulationOuterRadius);
            }

            //'determine where to start applying the radial soil cells based on whether we have insulation or not
            if (!SimHasInsulation) {
                MinimumSoilRadius = PipeOuterRadius;
            } else {
                MinimumSoilRadius = c.Insulation.OuterRadius;
            }

            //'the radial cells are distributed evenly throughout this region
            c.RadialSliceWidth = RadialGridExtent / NumRadialNodes;

            // first set Rval to the minimum soil radius plus half a slice thickness for the innermost radial node
            Real64 Rval = MinimumSoilRadius + (c.RadialSliceWidth / 2.0);
            Real64 ThisSliceInnerRadius = MinimumSoilRadius;
            c.Soil.emplace_back(Rval, ThisSliceInnerRadius, ThisSliceInnerRadius + c.RadialSliceWidth);

            //'then loop through the rest and assign them, each radius is simply one more slice thickness
            for (int RadialCellCtr = 1; RadialCellCtr < NumRadialNodes; ++RadialCellCtr) {
                Rval += c.RadialSliceWidth;
                ThisSliceInnerRadius += c.RadialSliceWidth;
                c.Soil.emplace_back(Rval, ThisSliceInnerRadius, ThisSliceInnerRadius + c.RadialSliceWidth);
            }

            //'also assign the interface cell surrounding the radial system
            c.InterfaceVolume = (1.0 - (DataGlobals::Pi / 4.0)) * pow_2(GridCellWidth) * CellDepth;
        }

        void Domain::developMesh() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            //'****** LAYOUT PARTITIONS ******'
            this->createPartitionCenterList();

            bool XPartitionsExist = !this->Partitions.X.empty();
            std::vector<GridRegion> XPartitionRegions = this->createPartitionRegionList(this->Partitions.X,
                                                                                        XPartitionsExist,
                                                                                        this->Extents.xMax);

            bool YPartitionsExist = !this->Partitions.Y.empty();
            std::vector<GridRegion> YPartitionRegions = this->createPartitionRegionList(this->Partitions.Y,
                                                                                        YPartitionsExist,
                                                                                        this->Extents.yMax);

            bool ZPartitionsExist = !this->Partitions.Z.empty();
            std::vector<GridRegion> ZPartitionRegions = this->createPartitionRegionList(this->Partitions.Z,
                                                                                        ZPartitionsExist,
                                                                                        this->Extents.zMax);

            //'***** LAYOUT MESH REGIONS *****'
            // Zone-coupled slab  models
            if (this->HasZoneCoupledBasement) {
                this->createRegionList(XRegions,
                                       XPartitionRegions,
                                       this->Extents.xMax,
                                       RegionType::XDirection,
                                       XPartitionsExist,
                                       _,
                                       _,
                                       this->XIndex,
                                       this->XWallIndex,
                                       this->InsulationXIndex);

                this->createRegionList(YRegions,
                                       YPartitionRegions,
                                       this->Extents.yMax,
                                       RegionType::YDirection,
                                       YPartitionsExist,
                                       _,
                                       _,
                                       _,
                                       _,
                                       _,
                                       this->YIndex,
                                       this->YFloorIndex,
                                       this->InsulationYIndex);

                this->createRegionList(ZRegions,
                                       ZPartitionRegions,
                                       this->Extents.zMax,
                                       RegionType::ZDirection,
                                       ZPartitionsExist,
                                       _,
                                       _,
                                       _,
                                       _,
                                       _,
                                       _,
                                       _,
                                       _,
                                       this->ZIndex,
                                       this->ZWallIndex,
                                       this->InsulationZIndex);
            } else if (this->HasZoneCoupledSlab) {
                this->createRegionList(XRegions,
                                       XPartitionRegions,
                                       this->Extents.xMax,
                                       RegionType::XDirection,
                                       XPartitionsExist,
                                       _,
                                       _,
                                       this->XIndex,
                                       _,
                                       this->InsulationXIndex);

                this->createRegionList(YRegions,
                                       YPartitionRegions,
                                       this->Extents.yMax,
                                       RegionType::YDirection,
                                       YPartitionsExist,
                                       _,
                                       _,
                                       _,
                                       _,
                                       _,
                                       this->YIndex,
                                       _,
                                       this->InsulationYIndex);

                this->createRegionList(ZRegions,
                                       ZPartitionRegions,
                                       this->Extents.zMax,
                                       RegionType::ZDirection,
                                       ZPartitionsExist,
                                       _,
                                       _,
                                       _,
                                       _,
                                       _,
                                       _,
                                       _,
                                       _,
                                       this->ZIndex,
                                       _,
                                       this->InsulationZIndex);
            } else {
                this->createRegionList(
                        XRegions, XPartitionRegions, this->Extents.xMax, RegionType::XDirection, XPartitionsExist,
                        this->BasementZone.BasementWallXIndex);

                this->createRegionList(
                        YRegions, YPartitionRegions, this->Extents.yMax, RegionType::YDirection, YPartitionsExist, _,
                        this->BasementZone.BasementFloorYIndex);

                this->createRegionList(ZRegions, ZPartitionRegions, this->Extents.zMax, RegionType::ZDirection,
                                       ZPartitionsExist);
            }

            //'** MAKE REGIONS > BOUNDARIES **'
            std::vector<Real64> XBoundaryPoints = CreateBoundaryList(XRegions, this->Extents.xMax,
                                                                     RegionType::XDirection);
            std::vector<Real64> YBoundaryPoints = CreateBoundaryList(YRegions, this->Extents.yMax,
                                                                     RegionType::YDirection);
            std::vector<Real64> ZBoundaryPoints = CreateBoundaryList(ZRegions, this->Extents.zMax,
                                                                     RegionType::ZDirection);

            //'****** DEVELOP CELL ARRAY *****'
            this->createCellArray(XBoundaryPoints, YBoundaryPoints, ZBoundaryPoints);

            //'***** SETUP CELL NEIGHBORS ****'
            this->setupCellNeighbors();

            //'** SET UP PIPE CIRCUIT CELLS **'
            this->setupPipeCircuitInOutCells();
        }

        void Domain::createPartitionCenterList() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            Real64 const BasementCellFraction(0.001); // the fraction of domain extent to use for the basement cells
            // actual dimension shouldn't matter for calculation purposes

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 BasementDistFromBottom;
            Real64 FloorLocation;
            Real64 UnderFloorLocation;
            Real64 PipeCellWidth;
            Real64 SurfCellWidth; // Basement surface...
            Real64 SideXLocation;
            Real64 SideXWallLocation;
            Real64 SideXInsulationLocation;
            Real64 SideZLocation;
            Real64 SideZWallLocation;
            Real64 SideZInsulationLocation;
            Real64 SlabDistFromBottom;
            Real64 YInsulationLocation;
            Real64 CellWidth(0.0);
            Real64 InterfaceCellWidth(0.008);

            // Object Data
            std::vector<MeshPartition> PreviousEntries;
            Segment ThisSegment;

            //'NOTE: pipe location y values have already been corrected to be measured from the bottom surface
            //'in input they are measured by depth, but internally they are referred to by distance from y = 0, or the bottom boundary
            for (auto &CircuitIndex: this->CircuitIndices) {
                auto &thisCircuit = circuits[CircuitIndex];

                // set up a convenience variable here
                //'account for the pipe and insulation if necessary
                if (!thisCircuit.HasInsulation) {
                    PipeCellWidth = thisCircuit.PipeSize.OuterDia;
                } else {
                    PipeCellWidth = thisCircuit.InsulationSize.OuterDia;
                }

                //'then add the radial mesh thickness on both sides of the pipe/insulation construct
                PipeCellWidth += 2 * thisCircuit.RadialMeshThickness;

                for (auto &segment : thisCircuit.pipeSegments) {
                    if (std::find(this->Partitions.X.begin(), this->Partitions.X.end(), segment->PipeLocation.X) ==
                        this->Partitions.X.end()) {
                        this->Partitions.X.emplace_back(segment->PipeLocation.X, PartitionType::Pipe, PipeCellWidth);
                    }
                    if (std::find(this->Partitions.Y.begin(), this->Partitions.Y.end(), segment->PipeLocation.Y) ==
                        this->Partitions.Y.end()) {
                        this->Partitions.Y.emplace_back(segment->PipeLocation.Y, PartitionType::Pipe, PipeCellWidth);
                    }
                }
            }

            // Underground Piping Systems Ground domain with basement interaction
            if (!this->HasZoneCoupledBasement) {
                if (this->HasBasement) { // FHX model
                    //'NOTE: the basement depth is still a depth from the ground surface, need to correct for this here
                    if (this->BasementZone.Width > 0) {
                        SurfCellWidth = this->Extents.xMax * BasementCellFraction;
                        if (std::find(this->Partitions.X.begin(), this->Partitions.X.end(), this->BasementZone.Width) ==
                            this->Partitions.X.end()) {
                            this->Partitions.X.emplace_back(this->BasementZone.Width, PartitionType::BasementWall,
                                                            SurfCellWidth);
                        }
                    }
                    if (this->BasementZone.Depth > 0) {
                        SurfCellWidth = this->Extents.yMax * BasementCellFraction;
                        BasementDistFromBottom = this->Extents.yMax - this->BasementZone.Depth;
                        if (std::find(this->Partitions.Y.begin(), this->Partitions.Y.end(), BasementDistFromBottom) ==
                            this->Partitions.Y.end()) {
                            this->Partitions.Y.emplace_back(BasementDistFromBottom, PartitionType::BasementFloor,
                                                            SurfCellWidth);
                        }
                    }
                }
            } else { // Zone-coupled basement model
                //'NOTE: the basement depth is still a depth from the ground surface, need to correct for this here
                if (this->BasementZone.Width > 0) {
                    // Create partitions at basement walls and horizontal insulation edges
                    CellWidth = this->VertInsThickness;
                    // Side X direction - Insulation layer
                    SideXLocation = this->PerimeterOffset - InterfaceCellWidth - CellWidth / 2.0;
                    // Side X direction - Basement Wall Interface
                    SideXWallLocation = this->PerimeterOffset - InterfaceCellWidth / 2.0;
                    if (this->HorizInsPresentFlag && !this->FullHorizInsPresent) {
                        // Insulation Edge in X direction
                        SideXInsulationLocation =
                                this->PerimeterOffset + this->HorizInsWidth + InterfaceCellWidth / 2.0;
                    } else {
                        SideXInsulationLocation = -1;
                    }
                    if (std::find(this->Partitions.X.begin(), this->Partitions.X.end(), this->BasementZone.Width) ==
                        this->Partitions.X.end()) {
                        // Partition at insulation edges in the X direction, if horizontal insulation present
                        if (this->HorizInsPresentFlag) {
                            if (!this->FullHorizInsPresent) {
                                // Side X direction - Insulation layer
                                this->Partitions.X.emplace_back(SideXLocation, PartitionType::XSide, CellWidth);
                                // Side X direction - Basement Wall interface
                                this->Partitions.X.emplace_back(SideXWallLocation, PartitionType::XSideWall,
                                                                InterfaceCellWidth);
                                // Insulation Edge X direction
                                this->Partitions.X.emplace_back(SideXInsulationLocation, PartitionType::HorizInsXSide,
                                                                InterfaceCellWidth);
                            } else {
                                // Side X direction - Insulation layer
                                this->Partitions.X.emplace_back(SideXLocation, PartitionType::XSide, CellWidth);
                                // Side X direction -Basement Wall interface
                                this->Partitions.X.emplace_back(SideXWallLocation, PartitionType::XSideWall,
                                                                InterfaceCellWidth);
                            }
                        } else {
                            // Side X direction - Insulation layer
                            this->Partitions.X.emplace_back(SideXLocation, PartitionType::XSide, CellWidth);
                            // Side X direction - Basement Wall interface
                            this->Partitions.X.emplace_back(SideXWallLocation, PartitionType::XSideWall,
                                                            InterfaceCellWidth);
                        }
                    }
                }
                // Zone coupled basement model
                if (this->BasementZone.Depth > 0) {
                    CellWidth = this->HorizInsThickness;
                    // Distance of basement floor interface from domain bottom
                    FloorLocation = this->Extents.yMax - this->BasementZone.Depth - InterfaceCellWidth / 2.0;
                    // Distance of basement floor insulation layer from domain bottom
                    UnderFloorLocation =
                            this->Extents.yMax - this->BasementZone.Depth - InterfaceCellWidth - CellWidth / 2.0;
                    if (this->VertInsPresentFlag) {
                        YInsulationLocation = this->Extents.yMax - this->VertInsDepth - InterfaceCellWidth / 2.0;
                    } else {
                        YInsulationLocation = -1;
                    }
                    if (std::find(this->Partitions.Y.begin(), this->Partitions.Y.end(), FloorLocation) ==
                        this->Partitions.Y.end()) {
                        // Partition at bottom edge of vertical insulation, if vertical insulation is present
                        if (this->VertInsPresentFlag && YInsulationLocation > FloorLocation + CellWidth) {
                            // Partition at basement floor interface
                            this->Partitions.Y.emplace_back(FloorLocation, PartitionType::FloorInside,
                                                            InterfaceCellWidth);
                            // Partition under the basement floor for insulation layer
                            this->Partitions.Y.emplace_back(UnderFloorLocation, PartitionType::UnderFloor, CellWidth);
                            // Vertical-Insulation edge partition
                            this->Partitions.Y.emplace_back(YInsulationLocation, PartitionType::VertInsLowerEdge,
                                                            InterfaceCellWidth);
                        } else {
                            this->Partitions.Y.emplace_back(FloorLocation, PartitionType::FloorInside,
                                                            InterfaceCellWidth);
                            this->Partitions.Y.emplace_back(UnderFloorLocation, PartitionType::UnderFloor, CellWidth);
                        }
                    }
                }
                if (this->BasementZone.Width > 0) {
                    // Create partitions at basement walls and horizontal insulation edges
                    CellWidth = this->VertInsThickness;
                    // Side Z direction - Insulation layer
                    SideZLocation = this->PerimeterOffset - InterfaceCellWidth - CellWidth / 2.0;
                    // Side Z direction - Basement Wall Interface
                    SideZWallLocation = this->PerimeterOffset - InterfaceCellWidth / 2.0;
                    if (this->HorizInsPresentFlag && !this->FullHorizInsPresent) {
                        // Insulation Edge Z direction
                        SideZInsulationLocation =
                                this->PerimeterOffset + this->HorizInsWidth + InterfaceCellWidth / 2.0;
                    } else {
                        SideZInsulationLocation = -1;
                    }
                    if (std::find(this->Partitions.Z.begin(), this->Partitions.Z.end(), this->BasementZone.Width) ==
                        this->Partitions.Z.end()) {
                        // Partition at insulation edges in the Z direction, if horizontal insulation present
                        if (this->HorizInsPresentFlag) {
                            if (!this->FullHorizInsPresent) {
                                // Side Z direction - Insulation layer
                                this->Partitions.Z.emplace_back(SideZLocation, PartitionType::ZSide, CellWidth);
                                // Side Z direction - Basement Wall interface
                                this->Partitions.Z.emplace_back(SideZWallLocation, PartitionType::ZSideWall,
                                                                InterfaceCellWidth);
                                // Insulation Edge Z direction
                                this->Partitions.Z.emplace_back(SideZInsulationLocation, PartitionType::HorizInsZSide,
                                                                InterfaceCellWidth);
                            } else {
                                // Side Z direction - Insulation layer
                                this->Partitions.Z.emplace_back(SideZLocation, PartitionType::ZSide, CellWidth);
                                // Side Z direction -Basement Wall interface
                                this->Partitions.Z.emplace_back(SideZWallLocation, PartitionType::ZSideWall,
                                                                InterfaceCellWidth);
                            }
                        } else {
                            // Side Z direction - Insulation layer
                            this->Partitions.Z.emplace_back(SideZLocation, PartitionType::ZSide, CellWidth);
                            // Side Z direction -Basement Wall interface
                            this->Partitions.Z.emplace_back(SideZWallLocation, PartitionType::ZSideWall,
                                                            InterfaceCellWidth);
                        }
                    }
                }
            }

            // Zone-coupled slab
            if (this->HasZoneCoupledSlab) {
                // NOTE: the slab depth is still a depth from the ground surface, need to correct for this here.

                // Create X-direction partitions

                // Create partition at slab edges in the X direction
                CellWidth = this->VertInsThickness;
                // Side X direction
                SideXLocation = this->PerimeterOffset - CellWidth / 2.0;
                // Insulation Edge X direction
                if (this->HorizInsPresentFlag && !this->FullHorizInsPresent) {
                    SideXInsulationLocation = SideXLocation + this->HorizInsWidth;
                } else {
                    SideXInsulationLocation = -1;
                }
                if (std::find(this->Partitions.X.begin(), this->Partitions.X.end(), this->SlabWidth) ==
                    this->Partitions.X.end()) {
                    // Partition at insulation edges in the X direction, if horizontal insulation present
                    if (this->HorizInsPresentFlag) {
                        if (!this->FullHorizInsPresent) {
                            // Side X direction
                            this->Partitions.X.emplace_back(SideXLocation, PartitionType::XSide, CellWidth);
                            // Insulation Edge X direction
                            this->Partitions.X.emplace_back(SideXInsulationLocation, PartitionType::HorizInsXSide,
                                                            CellWidth);
                        } else {
                            // Side X direction
                            this->Partitions.X.emplace_back(SideXLocation, PartitionType::XSide, CellWidth);
                        }
                    } else {
                        // Side X direction
                        this->Partitions.X.emplace_back(SideXLocation, PartitionType::XSide, CellWidth);
                    }
                }

                // Create Y-direction partitions

                CellWidth = this->VertInsThickness;

                // Partition at bottom edge of vertical insulation, if vertical insulation present
                if (this->VertInsPresentFlag) {
                    YInsulationLocation = this->Extents.yMax - this->VertInsDepth + CellWidth / 2.0;
                } else {
                    YInsulationLocation = -1;
                }

                if (this->SlabInGradeFlag) { // Slab in-grade case

                    SlabDistFromBottom = this->Extents.yMax - this->SlabThickness - CellWidth / 2.0;

                    if (std::find(this->Partitions.Y.begin(), this->Partitions.Y.end(), SlabDistFromBottom) ==
                        this->Partitions.Y.end()) {

                        // Partition at bottom edge of vertical insulation, if vertical insulation present
                        if (this->VertInsPresentFlag) {
                            // Under-slab partition
                            this->Partitions.Y.emplace_back(SlabDistFromBottom, PartitionType::UnderFloor, CellWidth);
                            // Vertical-Insulation edge partition
                            this->Partitions.Y.emplace_back(YInsulationLocation, PartitionType::VertInsLowerEdge,
                                                            CellWidth);
                        } else {
                            // Under-slab partition
                            this->Partitions.Y.emplace_back(SlabDistFromBottom, PartitionType::UnderFloor, CellWidth);
                        }
                    }
                } else { // Slab on-grade case

                    if (std::find(this->Partitions.Y.begin(), this->Partitions.Y.end(), YInsulationLocation) ==
                        this->Partitions.Y.end()) {
                        // Partition at bottom edge of vertical insulation, if vertical insulation present
                        if (this->VertInsPresentFlag) {
                            // Vertical-Insulation edge partition
                            this->Partitions.Y.emplace_back(YInsulationLocation, PartitionType::VertInsLowerEdge,
                                                            CellWidth);
                        }
                    }
                }

                // Create Z-direction partitions

                CellWidth = this->VertInsThickness;
                // Side Z direction
                SideZLocation = this->PerimeterOffset - CellWidth / 2.0;
                // Insulation Edge Z direction
                if (this->HorizInsPresentFlag && !this->FullHorizInsPresent) {
                    SideZInsulationLocation = SideZLocation + this->HorizInsWidth;
                } else {
                    SideZInsulationLocation = -1;
                }
                if (std::find(this->Partitions.Z.begin(), this->Partitions.Z.end(), this->SlabWidth) ==
                    this->Partitions.Z.end()) {
                    // Partition at insulation edges in the Z direction, if horizontal insulation present
                    if (this->HorizInsPresentFlag) {
                        if (!this->FullHorizInsPresent) {
                            // Side Z direction
                            this->Partitions.Z.emplace_back(SideZLocation, PartitionType::ZSide, CellWidth);
                            // Insulation Edge Z direction
                            this->Partitions.Z.emplace_back(SideZInsulationLocation, PartitionType::HorizInsZSide,
                                                            CellWidth);
                        } else {
                            // Side Z direction
                            this->Partitions.Z.emplace_back(SideZLocation, PartitionType::ZSide, CellWidth);
                        }
                    } else {
                        // Side Z direction
                        this->Partitions.Z.emplace_back(SideZLocation, PartitionType::ZSide, CellWidth);
                    }
                }
            }
            auto lambda = [](MeshPartition a, MeshPartition b) { return a.rDimension < b.rDimension; };
            std::sort(this->Partitions.X.begin(), this->Partitions.X.end(), lambda);
            std::sort(this->Partitions.Y.begin(), this->Partitions.Y.end(), lambda);
            std::sort(this->Partitions.Z.begin(), this->Partitions.Z.end(), lambda);
        }

        std::vector<GridRegion>
        Domain::createPartitionRegionList(std::vector<MeshPartition> const &ThesePartitionCenters,
                                          bool const PartitionsExist,
                                          Real64 const DirExtentMax) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // Return value
            std::vector<GridRegion> ThesePartitionRegions;

            // FUNCTION PARAMETER DEFINITIONS:
            static std::string const RoutineName("CreatePartitionRegionList");

            if (!PartitionsExist) {
                return ThesePartitionRegions;
            }

            //'loop across all partitions
            for (int Index = 0; Index < (int) ThesePartitionCenters.size(); ++Index) {
                auto &thisPartitionCenter = ThesePartitionCenters[Index];

                Real64 const ThisCellWidthBy2 = thisPartitionCenter.TotalWidth / 2.0;
                PartitionType ThisPartitionType = thisPartitionCenter.partitionType;

                //'use this half width to validate the region and add it to the collection
                Real64 CellLeft = thisPartitionCenter.rDimension - ThisCellWidthBy2;
                Real64 CellRight = thisPartitionCenter.rDimension + ThisCellWidthBy2;

                // check to make sure this location is valid
                if (CellLeft < 0.0 || CellRight > DirExtentMax) {
                    ShowSevereError("PlantPipingSystems::" + RoutineName + ": Invalid partition location in domain.");
                    ShowContinueError("Occurs during mesh development for domain=" + this->Name);
                    ShowContinueError("A pipe or basement is located outside of the domain extents.");
                    ShowFatalError("Preceding error causes program termination.");
                }

                // Scan all grid regions to make sure this range doesn't fall within an already entered range
                for (int SubIndex = 0; SubIndex <= Index - 1; ++SubIndex) {
                    auto &thisPartitionRegionSubIndex = ThesePartitionRegions[SubIndex];
                    // Coupled-basement model has adjacent partitions: ThesePartitionRegions( 0 ) and ThesePartitionRegions( 1 ) - SA
                    if (this->HasZoneCoupledBasement && Index == 1) {
                        if (IsInRange_BasementModel(CellLeft, thisPartitionRegionSubIndex.Min,
                                                    thisPartitionRegionSubIndex.Max) ||
                            IsInRangeReal(CellRight, thisPartitionRegionSubIndex.Min,
                                          thisPartitionRegionSubIndex.Max)) {

                            ShowSevereError(
                                    "PlantPipingSystems::" + RoutineName + ": Invalid partition location in domain.");
                            ShowContinueError("Occurs during mesh development for domain=" + this->Name);
                            ShowContinueError("A mesh conflict was encountered where partitions were overlapping.");
                            ShowContinueError(
                                    "Ensure that all pipes exactly line up or are separated to allow meshing in between them");
                            ShowContinueError("Also verify the pipe and basement dimensions to avoid conflicts there.");
                            ShowFatalError("Preceding error causes program termination");
                        }

                    } else {

                        if (IsInRangeReal(CellLeft, thisPartitionRegionSubIndex.Min, thisPartitionRegionSubIndex.Max) ||
                            IsInRangeReal(CellRight, thisPartitionRegionSubIndex.Min,
                                          thisPartitionRegionSubIndex.Max)) {

                            ShowSevereError(
                                    "PlantPipingSystems::" + RoutineName + ": Invalid partition location in domain.");
                            ShowContinueError("Occurs during mesh development for domain=" + this->Name);
                            ShowContinueError("A mesh conflict was encountered where partitions were overlapping.");
                            ShowContinueError(
                                    "Ensure that all pipes exactly line up or are separated to allow meshing in between them");
                            ShowContinueError("Also verify the pipe and basement dimensions to avoid conflicts there.");
                            ShowFatalError("Preceding error causes program termination");
                        }
                    }
                }

                ThesePartitionRegions.emplace_back();
                auto &thisNewPartitionRegion = ThesePartitionRegions[Index];
                thisNewPartitionRegion.Min = CellLeft;
                thisNewPartitionRegion.Max = CellRight;

                // Need to map partition type into region type parameters, since they are different enumerations
                if (ThisPartitionType == PartitionType::BasementWall) {
                    thisNewPartitionRegion.thisRegionType = RegionType::BasementWall;
                } else if (ThisPartitionType == PartitionType::BasementFloor) {
                    thisNewPartitionRegion.thisRegionType = RegionType::BasementFloor;
                } else if (ThisPartitionType == PartitionType::Pipe) {
                    thisNewPartitionRegion.thisRegionType = RegionType::Pipe;
                } else if (ThisPartitionType == PartitionType::XSide) {
                    thisNewPartitionRegion.thisRegionType = RegionType::XSide;
                } else if (ThisPartitionType == PartitionType::XSideWall) {
                    thisNewPartitionRegion.thisRegionType = RegionType::XSideWall;
                } else if (ThisPartitionType == PartitionType::HorizInsXSide) {
                    thisNewPartitionRegion.thisRegionType = RegionType::HorizInsXSide;
                } else if (ThisPartitionType == PartitionType::ZSide) {
                    thisNewPartitionRegion.thisRegionType = RegionType::ZSide;
                } else if (ThisPartitionType == PartitionType::ZSideWall) {
                    thisNewPartitionRegion.thisRegionType = RegionType::ZSideWall;
                } else if (ThisPartitionType == PartitionType::HorizInsZSide) {
                    thisNewPartitionRegion.thisRegionType = RegionType::HorizInsZSide;
                } else if (ThisPartitionType == PartitionType::FloorInside) {
                    thisNewPartitionRegion.thisRegionType = RegionType::FloorInside;
                } else if (ThisPartitionType == PartitionType::UnderFloor) {
                    thisNewPartitionRegion.thisRegionType = RegionType::UnderFloor;
                } else if (ThisPartitionType == PartitionType::VertInsLowerEdge) {
                    thisNewPartitionRegion.thisRegionType = RegionType::VertInsLowerEdge;
                } else {
                    // diagnostic error
                }
            }

            return ThesePartitionRegions;
        }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "ArgumentSelectionDefectsInspection"

        void Domain::createRegionList(std::vector<GridRegion> &Regions,
                                      std::vector<GridRegion> const &ThesePartitionRegions,
                                      Real64 const DirExtentMax,
                                      RegionType const DirDirection,
                                      bool const PartitionsExist,
                                      Optional_int BasementWallXIndex,
                                      Optional_int BasementFloorYIndex,
                                      Optional_int XIndex,
                                      Optional_int XWallIndex,
                                      Optional_int InsulationXIndex,
                                      Optional_int YIndex,
                                      Optional_int YFloorIndex,
                                      Optional_int InsulationYIndex,
                                      Optional_int ZIndex,
                                      Optional_int ZWallIndex,
                                      Optional_int InsulationZIndex) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            int cellCountUpToNow = 0;
            std::vector<Real64> tempCellWidths;

            if (PartitionsExist) {

                for (int i = 0; i < (int) ThesePartitionRegions.size(); ++i) {
                    auto &thisPartition(ThesePartitionRegions[i]);

                    if (i == 0) { // First partition
                        // Create region to left of partition
                        GridRegion tempRegion(0.0, thisPartition.Min, DirDirection, tempCellWidths);
                        int potentialCellWidthsCount = this->getCellWidthsCount(DirDirection);
                        if ((thisPartition.Min - 0.0) < 0.00001) {
                            cellCountUpToNow += 1; // just one cell for extremely tight regions
                        } else {
                            cellCountUpToNow += potentialCellWidthsCount;
                        }
                        this->getCellWidths(tempRegion, tempRegion.thisRegionType);
                        Regions.push_back(tempRegion);
                    } else if (i == 1 && this->HasZoneCoupledBasement) {
                        cellCountUpToNow += 1; // don't add a left partition for partition index 1 of coupled basements
                    } else {                   // All other partitions
                        // Because of the way the index block below is structured, we need to update cellCount
                        //  **after** we pass that block.  We could include logic below to do this, but this block
                        //  already fits within the structure properly, so increment it here to account for the
                        //  single cell partition layer that was applied at the **end** of the previous partition index
                        ++cellCountUpToNow;
                        // Create region to left of partition
                        auto &leftPartition(ThesePartitionRegions[i - 1]);
                        auto tempRegion(GridRegion(leftPartition.Max, thisPartition.Min, DirDirection, tempCellWidths));
                        int potentialCellWidthsCount = this->getCellWidthsCount(DirDirection);
                        if ((thisPartition.Min - leftPartition.Max) < 0.00001) {
                            cellCountUpToNow += 1; // just one cell for extremely tight regions
                        } else {
                            cellCountUpToNow += potentialCellWidthsCount;
                        }
                        this->getCellWidths(tempRegion, tempRegion.thisRegionType);
                        Regions.push_back(tempRegion);
                    }

                    if (thisPartition.thisRegionType == RegionType::BasementWall) {
                        if (present(BasementWallXIndex)) BasementWallXIndex = cellCountUpToNow;
                    } else if (thisPartition.thisRegionType == RegionType::BasementFloor) {
                        if (present(BasementFloorYIndex)) BasementFloorYIndex = cellCountUpToNow;
                    } else if (thisPartition.thisRegionType == RegionType::XSide) {
                        if (present(XIndex)) XIndex = cellCountUpToNow;
                        this->XIndex = XIndex;
                    } else if (thisPartition.thisRegionType == RegionType::XSideWall) {
                        if (present(XWallIndex)) XWallIndex = cellCountUpToNow;
                        this->XWallIndex = XWallIndex;
                    } else if (thisPartition.thisRegionType == RegionType::ZSide) {
                        if (present(ZIndex)) ZIndex = cellCountUpToNow;
                        this->ZIndex = ZIndex;
                    } else if (thisPartition.thisRegionType == RegionType::ZSideWall) {
                        if (present(ZWallIndex)) ZWallIndex = cellCountUpToNow;
                        this->ZWallIndex = ZWallIndex;
                    } else if (thisPartition.thisRegionType == RegionType::HorizInsXSide) {
                        if (present(InsulationXIndex)) InsulationXIndex = cellCountUpToNow;
                        this->InsulationXIndex = InsulationXIndex;
                    } else if (thisPartition.thisRegionType == RegionType::HorizInsZSide) {
                        if (present(InsulationZIndex)) InsulationZIndex = cellCountUpToNow;
                        this->InsulationZIndex = InsulationZIndex;
                    } else if (thisPartition.thisRegionType == RegionType::FloorInside) {
                        if (present(YFloorIndex)) YFloorIndex = cellCountUpToNow;
                        this->YFloorIndex = YFloorIndex;
                    } else if (thisPartition.thisRegionType == RegionType::UnderFloor) {
                        if (present(YIndex)) YIndex = cellCountUpToNow;
                        this->YIndex = YIndex;
                    } else if (thisPartition.thisRegionType == RegionType::VertInsLowerEdge) {
                        if (present(InsulationYIndex)) InsulationYIndex = cellCountUpToNow;
                        this->InsulationYIndex = InsulationYIndex;
                    }

                    // Create region for this partition
                    auto tempRegion(GridRegion(thisPartition.Min, thisPartition.Max, thisPartition.thisRegionType,
                                               tempCellWidths));
                    this->getCellWidths(tempRegion, tempRegion.thisRegionType);
                    Regions.push_back(tempRegion);
                }

                // Create final region
                auto &thisPartition(ThesePartitionRegions[ThesePartitionRegions.size() - 1]);
                auto tempRegion(GridRegion(thisPartition.Max, DirExtentMax, DirDirection, tempCellWidths));
                this->getCellWidths(tempRegion, tempRegion.thisRegionType);
                Regions.push_back(tempRegion);

            } else {
                // Need to create a region anyway if no partitions exist
                auto tempRegion(GridRegion(0.0, DirExtentMax, DirDirection, tempCellWidths));
                this->getCellWidths(tempRegion, tempRegion.thisRegionType);
                Regions.push_back(tempRegion);
            }
        }

#pragma clang diagnostic pop

        std::vector<Real64> CreateBoundaryList(std::vector<GridRegion> const &RegionList,
                                               Real64 const DirExtentMax,
                                               RegionType const DirDirection) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            std::vector<Real64> RetVal;
            for (auto const &thisRegion : RegionList) {
                switch (thisRegion.thisRegionType) {
                    case RegionType::Pipe:
                    case RegionType::BasementFloor:
                    case RegionType::BasementWall:
                    case RegionType::XSide:
                    case RegionType::XSideWall:
                    case RegionType::ZSide:
                    case RegionType::ZSideWall:
                    case RegionType::HorizInsXSide:
                    case RegionType::HorizInsZSide:
                    case RegionType::FloorInside:
                    case RegionType::UnderFloor:
                    case RegionType::VertInsLowerEdge:
                        RetVal.push_back(thisRegion.Min);
                        break;
                    default:
                        if (thisRegion.thisRegionType == DirDirection) {
                            Real64 StartingPointCounter = thisRegion.Min;
                            for (auto &cellWidth : thisRegion.CellWidths) {
                                RetVal.push_back(StartingPointCounter);
                                StartingPointCounter += cellWidth;
                            }
                        }
                }
            }
            RetVal.push_back(DirExtentMax);
            return RetVal;
        }

        void Domain::createCellArray(std::vector<Real64> const &XBoundaryPoints,
                                     std::vector<Real64> const &YBoundaryPoints,
                                     std::vector<Real64> const &ZBoundaryPoints) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            int TotNumCells = 0;
            int NumCutawayBasementCells = 0;
            int NumInsulationCells = 0;
            int NumGroundSurfaceCells = 0;

            struct tCellExtents : MeshExtents {
                // Members
                Real64 Xmin;
                Real64 Ymin;
                Real64 Zmin;

                // Member Constructor
                tCellExtents(Real64 const Xmax, Real64 const Ymax, Real64 const Zmax, Real64 const Xmin,
                             Real64 const Ymin, Real64 const Zmin)
                        : MeshExtents(Xmax, Ymax, Zmax), Xmin(Xmin), Ymin(Ymin), Zmin(Zmin) {
                }
            };

            //'subtract 2 in each dimension:
            //'     one for zero based array
            //'     one because the boundary points contain one entry more than the number of cells WITHIN the domain
            this->x_max_index = XBoundaryPoints.size() - 2;
            this->y_max_index = YBoundaryPoints.size() - 2;
            this->z_max_index = ZBoundaryPoints.size() - 2;
            this->Cells.allocate({0, this->x_max_index}, {0, this->y_max_index}, {0, this->z_max_index});

            int MaxBasementXNodeIndex = this->BasementZone.BasementWallXIndex;
            int MinBasementYNodeIndex = this->BasementZone.BasementFloorYIndex;
            int MinXIndex = this->XIndex;
            int YIndex = this->YIndex;
            int MinZIndex = this->ZIndex;
            int XWallIndex = this->XWallIndex;
            int YFloorIndex = this->YFloorIndex;
            int ZWallIndex = this->ZWallIndex;
            int InsulationXIndex = this->InsulationXIndex;
            int InsulationYIndex = this->InsulationYIndex;
            int InsulationZIndex = this->InsulationZIndex;

            auto &cells(this->Cells);
            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto &cell(cells(X, Y, Z));

                        //'set up x-direction variables
                        int CellXIndex = X;                            //'zero based index
                        Real64 CellXMinValue = XBoundaryPoints[X];     //'left wall x-value
                        Real64 CellXMaxValue = XBoundaryPoints[X + 1]; //'right wall x-value
                        Real64 CellXCenter = (CellXMinValue + CellXMaxValue) / 2;
                        Real64 CellWidth = CellXMaxValue - CellXMinValue;

                        //'set up y-direction variables
                        int CellYIndex = Y;                            //'zero based index
                        Real64 CellYMinValue = YBoundaryPoints[Y];     //'bottom wall y-value
                        Real64 CellYMaxValue = YBoundaryPoints[Y + 1]; //'top wall y-value
                        Real64 CellYCenter = (CellYMinValue + CellYMaxValue) / 2;
                        Real64 CellHeight = CellYMaxValue - CellYMinValue;

                        //'set up z-direction variables
                        int CellZIndex = Z;                            //'zero based index
                        Real64 CellZMinValue = ZBoundaryPoints[Z];     //'lower z value
                        Real64 CellZMaxValue = ZBoundaryPoints[Z + 1]; //'higher z value
                        Real64 CellZCenter = (CellZMinValue + CellZMaxValue) / 2;

                        //'set up an extent class for this cell
                        tCellExtents CellExtents = tCellExtents(CellXMaxValue, CellYMaxValue, CellZMaxValue,
                                                                CellXMinValue, CellYMinValue, CellZMinValue);

                        //'set up centroid, index, and overall size
                        Point3DReal Centroid = Point3DReal(CellXCenter, CellYCenter, CellZCenter);
                        Point3DInteger CellIndeces = Point3DInteger(CellXIndex, CellYIndex, CellZIndex);
                        RectangleF XYRectangle = RectangleF(CellXMinValue, CellYMinValue, CellWidth, CellHeight);

                        //'determine cell type
                        CellType cellType = CellType::Unknown;

                        //'if this is a pipe node, some flags are needed
                        bool pipeCell = false;
                        int NumRadialCells = -1;
                        int CircuitIndex = -1;

                        // Adiabatic behavior is now achieved in the SetupCellNeighbors routine, these are simply farfield for now.
                        CellType const ZWallCellType = CellType::FarfieldBoundary;
                        CellType const UnderBasementBoundary = CellType::FarfieldBoundary;

                        //'apply boundary conditions

                        // For zone-coupled ground domain
                        if (this->HasZoneCoupledSlab) {

                            // Assign all cells common between on-grade and in-grade cases first
                            // This should be all X/Z-side interface, vertical insulation, far-field,
                            // ground surface, and ground/zone interface cells
                            if (CellXIndex == MinXIndex && CellZIndex >= MinZIndex) { // Z side interface
                                // Check if vertical insulation present
                                if (this->VertInsPresentFlag) {
                                    if (CellYIndex <= this->y_max_index &&
                                        CellYIndex >= InsulationYIndex) { // Check depth of vertical insulation
                                        cellType = CellType::VertInsulation;
                                        ++NumInsulationCells;
                                    }
                                } else if (CellYIndex == this->y_max_index) {
                                    cellType = CellType::GroundSurface;
                                    ++NumGroundSurfaceCells;
                                }
                            } else if (CellZIndex == MinZIndex &&
                                       CellXIndex >= MinXIndex) {                 // X side interface
                                if (this->VertInsPresentFlag) {                                              // Check if vertical insulation present
                                    if (CellYIndex <= this->y_max_index &&
                                        CellYIndex >= InsulationYIndex) { // Check depth of vertical insulation
                                        cellType = CellType::VertInsulation;
                                        ++NumInsulationCells;
                                    }
                                } else if (CellYIndex == this->y_max_index) {
                                    cellType = CellType::GroundSurface;
                                    ++NumGroundSurfaceCells;
                                }
                            } else if (CellYIndex == y_max_index) {
                                if (CellXIndex <= MinXIndex || CellZIndex <= MinZIndex) { // Ground surface
                                    cellType = CellType::GroundSurface;
                                    ++NumGroundSurfaceCells;
                                } else if (CellXIndex >= MinXIndex ||
                                           CellZIndex >= MinZIndex) { // Zone-ground interface
                                    cellType = CellType::ZoneGroundInterface;
                                }
                            }

                            if (CellYIndex == 0 || CellXIndex == 0 || CellZIndex == 0) { // Farfield boundary
                                cellType = CellType::FarfieldBoundary;
                            }

                            // Assign different cells between in-grade and on-grade cases
                            if (this->SlabInGradeFlag) { // In-grade case
                                // This will assign the slab cells and horizontal insulation

                                if (CellZIndex > MinZIndex &&
                                    CellXIndex > MinXIndex) {     // Cells inside bounds of slab
                                    if (CellYIndex >= YIndex && CellYIndex < y_max_index) { // Slab cells
                                        cellType = CellType::Slab;
                                    } else if (CellYIndex == (YIndex - 1)) {
                                        if (this->HorizInsPresentFlag &&
                                            this->FullHorizInsPresent) { // Full under-slab insulation
                                            cellType = CellType::HorizInsulation;
                                        } else if (this->HorizInsPresentFlag &&
                                                   !this->FullHorizInsPresent) { // Perimeter only under-slab insulation
                                            if (CellZIndex < InsulationZIndex || CellXIndex < InsulationXIndex) {
                                                cellType = CellType::HorizInsulation;
                                            }
                                        }
                                    }
                                }

                            } else { // Slab-on grade
                                // Nothing should happen. Interface cells should already be set.
                                // Under that are 'General' field cells that should be caught later.
                            }

                        } else if (this->HasZoneCoupledBasement) { // basement model, zone-coupled
                            // Set the appropriate cell type
                            if (CellYIndex == 0) { // Farfield cells
                                cellType = CellType::FarfieldBoundary;
                            } else if (CellXIndex > XWallIndex && CellZIndex > ZWallIndex) {       // Basement cutaway
                                if (CellYIndex <= this->y_max_index &&
                                    CellYIndex > YFloorIndex) { // General basement cells
                                    cellType = CellType::BasementCutaway;
                                    // Not counting basement cutaway cells.
                                } else if (CellYIndex == YFloorIndex) { // Basement Floor cells
                                    cellType = CellType::BasementFloor;
                                } else if (CellYIndex == YIndex) {
                                    // Check if horizontal insulation present
                                    if (this->HorizInsPresentFlag) {
                                        if (this->FullHorizInsPresent) { // Entire underfloor insulated
                                            cellType = CellType::HorizInsulation;
                                            ++NumInsulationCells;
                                        } else { // Perimeter insulation
                                            if (CellXIndex < InsulationXIndex || CellZIndex < InsulationZIndex) {
                                                cellType = CellType::HorizInsulation;
                                                ++NumInsulationCells;
                                            }
                                        }
                                    }
                                }
                            } else if ((CellXIndex == XWallIndex && CellZIndex > ZWallIndex) ||
                                       (CellZIndex == ZWallIndex && CellXIndex > XWallIndex)) { // Basement Walls
                                if (CellYIndex <= this->y_max_index && CellYIndex > YFloorIndex) {
                                    cellType = CellType::BasementWall;
                                }
                            } else if ((CellXIndex == MinXIndex && CellZIndex > ZWallIndex) ||
                                       (CellZIndex == MinZIndex && CellXIndex > XWallIndex)) { // Insulation cells
                                if (CellYIndex <= this->y_max_index && CellYIndex > YFloorIndex) {
                                    // Check if vertical insulation present
                                    if (this->VertInsPresentFlag) {
                                        if (InsulationYIndex != 0) { // Partial vertical insulation
                                            if (CellYIndex <= this->y_max_index && CellYIndex > InsulationYIndex) {
                                                cellType = CellType::VertInsulation;
                                                ++NumInsulationCells;
                                            }
                                        } else { // Vertical insulation extends to depth of basement floor
                                            if (CellYIndex <= this->y_max_index && CellYIndex > YFloorIndex) {
                                                cellType = CellType::VertInsulation;
                                                ++NumInsulationCells;
                                            }
                                        }
                                    }
                                }
                            } else if (CellYIndex == this->y_max_index) { // Surface cells
                                cellType = CellType::GroundSurface;
                                ++NumGroundSurfaceCells;
                            } else if (CellYIndex == 0 || CellXIndex == 0 || CellZIndex == 0) { // Farfield boundary
                                cellType = CellType::FarfieldBoundary;
                            }
                        } else if (CellXIndex == MaxBasementXNodeIndex && CellYIndex == MinBasementYNodeIndex) {
                            cellType = CellType::BasementCorner;
                        } else if (CellXIndex == MaxBasementXNodeIndex && CellYIndex > MinBasementYNodeIndex) {
                            cellType = CellType::BasementWall;
                        } else if (CellXIndex < MaxBasementXNodeIndex && CellYIndex == MinBasementYNodeIndex) {
                            cellType = CellType::BasementFloor;
                        } else if (CellXIndex < MaxBasementXNodeIndex && CellYIndex > MinBasementYNodeIndex) {
                            cellType = CellType::BasementCutaway;
                            // Not counting basement cutaway cells
                        } else if (CellYIndex == Y_end) {
                            cellType = CellType::GroundSurface;
                            ++NumGroundSurfaceCells;
                        } else if (CellXIndex == 0) {
                            if (this->HasBasement && Y > 0) {
                                cellType = UnderBasementBoundary; //'this must come after the basement cutaway ELSEIF branch
                            } else {
                                cellType = CellType::FarfieldBoundary;
                            }
                        } else if (CellXIndex == X_end || CellYIndex == 0) {
                            cellType = CellType::FarfieldBoundary;
                        } else if (CellZIndex == 0 || CellZIndex == Z_end) {
                            cellType = ZWallCellType;
                        }

                        //'check to see if this is a pipe node...
                        Real64 InsulationThickness(0.0);
                        Real64 RadialMeshThickness(0.0);
                        bool HasInsulation(false);
                        RadialSizing PipeSizing;
                        for (auto &FoundOnCircuitIndex : this->CircuitIndices) {
                            for (auto &segment : circuits[FoundOnCircuitIndex].pipeSegments) {
                                if (XYRectangle.contains(segment->PipeLocation)) {
                                    //'inform the cell that it is a pipe node
                                    cellType = CellType::Pipe;
                                    //'inform the cell of which pipe it contains
                                    pipeCell = true;
                                    //'inform the cell of which pipe circuit contains it
                                    CircuitIndex = FoundOnCircuitIndex;
                                    //'inform the pipe of what cell it is inside
                                    segment->initPipeCells(CellXIndex, CellYIndex);
                                    //'set the number of cells to be generated in this near-pipe region
                                    NumRadialCells = circuits[FoundOnCircuitIndex].NumRadialCells;
                                    //'exit the pipe counter loop
                                    goto CircuitLoop_exit;
                                }
                            }
                        }
                        CircuitLoop_exit:;

                        //'if it still isn't anything, then it is just an interior node
                        switch (cellType) {
                            case CellType::BasementCutaway:
                                ++NumCutawayBasementCells;
                                break;
                            case CellType::Unknown:
                                cellType = CellType::GeneralField;
                                // fallthrough
                            default:
                                ++TotNumCells;
                        }

                        // if we were found on a pipe circuit, get some things for convenience
                        if (CircuitIndex != -1) {
                            auto const &thisCircuit = circuits[CircuitIndex];
                            if (thisCircuit.HasInsulation) {
                                InsulationThickness = thisCircuit.InsulationSize.thickness();
                            }
                            PipeSizing = thisCircuit.PipeSize;
                            RadialMeshThickness = thisCircuit.RadialMeshThickness;
                            HasInsulation = thisCircuit.HasInsulation;
                        }

                        //'instantiate the cell class
                        cell.X_min = CellExtents.Xmin;
                        cell.X_max = CellExtents.xMax;
                        cell.Y_min = CellExtents.Ymin;
                        cell.Y_max = CellExtents.yMax;
                        cell.Z_min = CellExtents.Zmin;
                        cell.Z_max = CellExtents.zMax;
                        cell.X_index = CellIndeces.X;
                        cell.Y_index = CellIndeces.Y;
                        cell.Z_index = CellIndeces.Z;
                        cell.Centroid = Centroid;
                        cell.cellType = cellType;

                        if (pipeCell) {
                            CartesianPipeCellInformation::ctor(cell.PipeCellData,
                                                               cell.X_max - cell.X_min,
                                                               PipeSizing,
                                                               NumRadialCells,
                                                               cell.depth(),
                                                               InsulationThickness,
                                                               RadialMeshThickness,
                                                               HasInsulation);
                        }

                    } //'z
                }     //'y
            }         //'x

            this->NumDomainCells = TotNumCells;
            this->NumGroundSurfCells = NumGroundSurfaceCells;
            this->NumInsulationCells = NumInsulationCells;
        }

        void Domain::setupCellNeighbors() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 CellRightCentroidX;
            Real64 CellRightLeftWallX;
            Real64 CellLeftCentroidX;
            Real64 CellLeftRightWallX;
            Real64 LeftCellCentroidX;
            Real64 LeftCellRightWallX;
            Real64 RightCellCentroidX;
            Real64 RightCellLeftWallX;
            Real64 UpperCellCentroidY;
            Real64 UpperCellLowerWallY;
            Real64 LowerCellCentroidY;
            Real64 LowerCellUpperWallY;
            Real64 UpperZCellCentroidZ;
            Real64 UpperZCellLowerWallZ;
            Real64 LowerZCellCentroidZ;
            Real64 LowerZCellUpperWallZ;

            auto const &cells(this->Cells);
            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto const &cell(cells(X, Y, Z));

                        //'for convenience
                        Real64 const &ThisCellCentroidX = cell.Centroid.X;
                        Real64 const &ThisCellCentroidY = cell.Centroid.Y;
                        Real64 const &ThisCellCentroidZ = cell.Centroid.Z;
                        Real64 ThisAdiabaticMultiplier = 1.0;
                        Real64 ThisAdiabaticMultiplierMirror = 1.0;

                        //'setup east/west cell neighbors
                        if (X == 0) {
                            CellRightCentroidX = cells(X + 1, Y, Z).Centroid.X;
                            CellRightLeftWallX = cells(X + 1, Y, Z).X_min;
                            // on the X=0 face, the only adiabatic cases are:
                            //   1) For a non-zone-coupled basement simulation, where the under basement X=0 cells are adiabatic -- cutaways will also get
                            //   adiabatic, but who cares?
                            if (((!this->HasZoneCoupledSlab) && (!this->HasZoneCoupledBasement) &&
                                 (this->HasBasement))) {
                                ThisAdiabaticMultiplier = 2.0;
                                ThisAdiabaticMultiplierMirror = 0.0;
                            }
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::PositiveX,
                                                         CellRightLeftWallX - ThisCellCentroidX,
                                                         CellRightCentroidX - CellRightLeftWallX,
                                                         ThisAdiabaticMultiplier);
                            this->addNeighborInformation(X, Y, Z, Direction::NegativeX, 0.0, 0.0,
                                                         ThisAdiabaticMultiplierMirror);
                        } else if (X == this->x_max_index) {
                            // on the X=XMAX face, the adiabatic cases are:
                            //   1) if we are doing a zone coupled slab/basement simulation where we quartered the domain
                            if (this->HasZoneCoupledSlab || this->HasZoneCoupledBasement) {
                                ThisAdiabaticMultiplier = 2.0;
                                ThisAdiabaticMultiplierMirror = 0.0;
                            }
                            CellLeftCentroidX = cells(X - 1, Y, Z).Centroid.X;
                            CellLeftRightWallX = cells(X - 1, Y, Z).X_max;
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::NegativeX,
                                                         ThisCellCentroidX - CellLeftRightWallX,
                                                         CellLeftRightWallX - CellLeftCentroidX,
                                                         ThisAdiabaticMultiplier);
                            this->addNeighborInformation(X, Y, Z, Direction::PositiveX, 0.0, 0.0,
                                                         ThisAdiabaticMultiplierMirror);
                        } else {
                            LeftCellCentroidX = cells(X - 1, Y, Z).Centroid.X;
                            LeftCellRightWallX = cells(X - 1, Y, Z).X_max;
                            RightCellCentroidX = cells(X + 1, Y, Z).Centroid.X;
                            RightCellLeftWallX = cells(X + 1, Y, Z).X_min;
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::NegativeX,
                                                         ThisCellCentroidX - LeftCellRightWallX,
                                                         LeftCellRightWallX - LeftCellCentroidX,
                                                         ThisAdiabaticMultiplier);
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::PositiveX,
                                                         RightCellLeftWallX - ThisCellCentroidX,
                                                         RightCellCentroidX - RightCellLeftWallX,
                                                         ThisAdiabaticMultiplier);
                        }

                        // Reset for the Y direction assignments
                        ThisAdiabaticMultiplier = 1.0;
                        ThisAdiabaticMultiplierMirror = 1.0;

                        //'setup north/south cell neighbors
                        if (Y == 0) {
                            UpperCellCentroidY = cells(X, Y + 1, Z).Centroid.Y;
                            UpperCellLowerWallY = cells(X, Y + 1, Z).Y_min;
                            // on the Y=0 face, the only adiabatic cases are:
                            //   1) NONE
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::PositiveY,
                                                         UpperCellLowerWallY - ThisCellCentroidY,
                                                         UpperCellCentroidY - UpperCellLowerWallY,
                                                         ThisAdiabaticMultiplier);
                            this->addNeighborInformation(X, Y, Z, Direction::NegativeY, 0.0, 0.0,
                                                         ThisAdiabaticMultiplierMirror);
                        } else if (Y == this->y_max_index) {
                            LowerCellCentroidY = cells(X, Y - 1, Z).Centroid.Y;
                            LowerCellUpperWallY = cells(X, Y - 1, Z).Y_max;
                            // on the Y=YMAX face, the only adiabatic cases are:
                            //   1) NONE
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::NegativeY,
                                                         ThisCellCentroidY - LowerCellUpperWallY,
                                                         LowerCellUpperWallY - LowerCellCentroidY,
                                                         ThisAdiabaticMultiplier);
                            this->addNeighborInformation(X, Y, Z, Direction::PositiveY, 0.0, 0.0,
                                                         ThisAdiabaticMultiplierMirror);
                        } else {
                            UpperCellCentroidY = cells(X, Y + 1, Z).Centroid.Y;
                            LowerCellCentroidY = cells(X, Y - 1, Z).Centroid.Y;
                            UpperCellLowerWallY = cells(X, Y + 1, Z).Y_min;
                            LowerCellUpperWallY = cells(X, Y - 1, Z).Y_max;
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::NegativeY,
                                                         ThisCellCentroidY - LowerCellUpperWallY,
                                                         LowerCellUpperWallY - LowerCellCentroidY,
                                                         ThisAdiabaticMultiplier);
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::PositiveY,
                                                         UpperCellLowerWallY - ThisCellCentroidY,
                                                         UpperCellCentroidY - UpperCellLowerWallY,
                                                         ThisAdiabaticMultiplier);
                        }

                        // Reset for the Z direction assignments
                        ThisAdiabaticMultiplier = 1.0;
                        ThisAdiabaticMultiplierMirror = 1.0;

                        //'setup forward/backward cell neighbors
                        if (Z == 0) {
                            UpperZCellCentroidZ = cells(X, Y, Z + 1).Centroid.Z;
                            UpperZCellLowerWallZ = cells(X, Y, Z + 1).Z_min;
                            // on the Z=0 face, the only adiabatic cases are:
                            //   1) for a non-zone-related simulation, such as for a standalone ground HX, or if we have the regular HasBasement
                            //   simulation
                            if (((!this->HasZoneCoupledSlab) && (!this->HasZoneCoupledBasement))) {
                                ThisAdiabaticMultiplier = 2.0;
                                ThisAdiabaticMultiplierMirror = 0.0;
                            }
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::PositiveZ,
                                                         UpperZCellLowerWallZ - ThisCellCentroidZ,
                                                         UpperZCellCentroidZ - UpperZCellLowerWallZ,
                                                         ThisAdiabaticMultiplier);
                            this->addNeighborInformation(X, Y, Z, Direction::NegativeZ, 0.0, 0.0,
                                                         ThisAdiabaticMultiplierMirror);
                        } else if (Z == this->z_max_index) {
                            LowerZCellCentroidZ = cells(X, Y, Z - 1).Centroid.Z;
                            LowerZCellUpperWallZ = cells(X, Y, Z - 1).Z_max;
                            // on the Z=ZMAX face, the only adiabatic cases are:
                            //   1) this face is always adiabatic?
                            // if (  ) {
                            ThisAdiabaticMultiplier = 2.0;
                            ThisAdiabaticMultiplierMirror = 0.0;
                            //}
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::NegativeZ,
                                                         ThisCellCentroidZ - LowerZCellUpperWallZ,
                                                         LowerZCellUpperWallZ - LowerZCellCentroidZ,
                                                         ThisAdiabaticMultiplier);
                            this->addNeighborInformation(X, Y, Z, Direction::PositiveZ, 0.0, 0.0,
                                                         ThisAdiabaticMultiplierMirror);
                        } else {
                            LowerZCellCentroidZ = cells(X, Y, Z - 1).Centroid.Z;
                            UpperZCellCentroidZ = cells(X, Y, Z + 1).Centroid.Z;
                            UpperZCellLowerWallZ = cells(X, Y, Z + 1).Z_min;
                            LowerZCellUpperWallZ = cells(X, Y, Z - 1).Z_max;
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::NegativeZ,
                                                         ThisCellCentroidZ - LowerZCellUpperWallZ,
                                                         LowerZCellUpperWallZ - LowerZCellCentroidZ,
                                                         ThisAdiabaticMultiplier);
                            this->addNeighborInformation(X,
                                                         Y,
                                                         Z,
                                                         Direction::PositiveZ,
                                                         UpperZCellLowerWallZ - ThisCellCentroidZ,
                                                         UpperZCellCentroidZ - UpperZCellLowerWallZ,
                                                         ThisAdiabaticMultiplier);
                        }
                    }
                }
            }
        }

        void Domain::addNeighborInformation(int const X,
                                            int const Y,
                                            int const Z,
                                            Direction const direction,
                                            Real64 const ThisCentroidToNeighborWall,
                                            Real64 const ThisWallToNeighborCentroid,
                                            Real64 const ThisAdiabaticMultiplier) {
            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na
            auto &thisNeighborInfo = this->Cells(X, Y, Z).NeighborInfo[direction];
            thisNeighborInfo.direction = direction;
            thisNeighborInfo.ThisCentroidToNeighborWall = ThisCentroidToNeighborWall;
            thisNeighborInfo.ThisWallToNeighborCentroid = ThisWallToNeighborCentroid;
            thisNeighborInfo.adiabaticMultiplier = ThisAdiabaticMultiplier;
        }

        void Domain::setupPipeCircuitInOutCells() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            auto const &cells(this->Cells);
            for (auto &CircuitIndex : this->CircuitIndices) {

                int SegmentInletCellX = 0;
                int SegmentInletCellY = 0;
                int SegmentInletCellZ = 0;
                int SegmentOutletCellX = 0;
                int SegmentOutletCellY = 0;
                int SegmentOutletCellZ = 0;
                int CircuitInletCellX = 0;
                int CircuitInletCellY = 0;
                int CircuitInletCellZ = 0;
                int CircuitOutletCellX = 0;
                int CircuitOutletCellY = 0;
                int CircuitOutletCellZ = 0;

                bool CircuitInletCellSet = false;

                for (auto &segment : circuits[CircuitIndex].pipeSegments) {
                    switch (segment->FlowDirection) {
                        case SegmentFlow::IncreasingZ:
                            SegmentInletCellX = segment->PipeCellCoordinates.X;
                            SegmentInletCellY = segment->PipeCellCoordinates.Y;
                            SegmentInletCellZ = 0;
                            SegmentOutletCellX = segment->PipeCellCoordinates.X;
                            SegmentOutletCellY = segment->PipeCellCoordinates.Y;
                            SegmentOutletCellZ = this->z_max_index;
                            break;
                        case SegmentFlow::DecreasingZ:
                            SegmentInletCellX = segment->PipeCellCoordinates.X;
                            SegmentInletCellY = segment->PipeCellCoordinates.Y;
                            SegmentInletCellZ = this->z_max_index;
                            SegmentOutletCellX = segment->PipeCellCoordinates.X;
                            SegmentOutletCellY = segment->PipeCellCoordinates.Y;
                            SegmentOutletCellZ = 0;
                            break;
                    }
                    if (!CircuitInletCellSet) {
                        CircuitInletCellX = SegmentInletCellX;
                        CircuitInletCellY = SegmentInletCellY;
                        CircuitInletCellZ = SegmentInletCellZ;
                        CircuitInletCellSet = true;
                    }
                    CircuitOutletCellX = SegmentOutletCellX;
                    CircuitOutletCellY = SegmentOutletCellY;
                    CircuitOutletCellZ = SegmentOutletCellZ;
                }

                circuits[CircuitIndex]
                        .initInOutCells(cells(CircuitInletCellX, CircuitInletCellY, CircuitInletCellZ),
                                        cells(CircuitOutletCellX, CircuitOutletCellY, CircuitOutletCellZ));
            }
        }

        int Domain::getCellWidthsCount(RegionType const dir) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            if (dir == RegionType::XDirection) {
                return this->Mesh.X.RegionMeshCount;
            } else if (dir == RegionType::YDirection) {
                return this->Mesh.Y.RegionMeshCount;
            } else if (dir == RegionType::ZDirection) {
                return this->Mesh.Z.RegionMeshCount;
            } else {
                return 1; // it's either a mesh region (X,Y,ZDirection), or it is some form of partition -- so 1
            }
            return 0;
        }

        void Domain::getCellWidths(GridRegion &g, RegionType const direction) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // Object Data
            DistributionStructure ThisMesh;
            ThisMesh.RegionMeshCount = 0;
            ThisMesh.GeometricSeriesCoefficient = 0.0;

            switch (direction) {
                case RegionType::XDirection:
                    ThisMesh = this->Mesh.X;
                    break;
                case RegionType::YDirection:
                    ThisMesh = this->Mesh.Y;
                    break;
                case RegionType::ZDirection:
                    ThisMesh = this->Mesh.Z;
                    break;
                default:
                    ThisMesh.RegionMeshCount = 1; // it must be a partition type or something
                    ThisMesh.thisMeshDistribution = MeshDistribution::Uniform;
                    // ShowSevereError( "Invalid RegionType passed to PlantPipingSystems::Domain::getCellWidths; should be x, y, or z
                    // direction only." );  ShowContinueError( "This is a developer problem, as the code should never reach this point." );  ShowFatalError(
                    // "EnergyPlus aborts due to the previous severe error" );
            }

            // just one cell for extremely tight regions
            if ((g.Max - g.Min) < 0.00001) {
                ThisMesh.RegionMeshCount = 1;
                ThisMesh.thisMeshDistribution = MeshDistribution::Uniform;
            }
            assert(g.Max > g.Min);

            Real64 GridWidth = g.Max - g.Min;

            if (ThisMesh.thisMeshDistribution == MeshDistribution::Uniform) {
                if (this->HasZoneCoupledSlab && g.thisRegionType == RegionType::YDirection &&
                    g.Max == this->Extents.yMax) { // Slab region
                    Real64 const CellWidth = GridWidth / this->NumSlabCells;
                    for (int I = 0; I <= this->NumSlabCells - 1; ++I) {
                        g.CellWidths.push_back(CellWidth);
                    }
                } else {
                    // we have it quite simple
                    assert(ThisMesh.RegionMeshCount > 0);
                    Real64 const CellWidth = GridWidth / ThisMesh.RegionMeshCount;
                    for (int I = 0; I <= ThisMesh.RegionMeshCount - 1; ++I) {
                        g.CellWidths.push_back(CellWidth);
                    }
                }
            } else if (ThisMesh.thisMeshDistribution == MeshDistribution::SymmetricGeometric) {

                //'then apply this "direction"'s conditions to generate a cell width array
                //'first get the total number of cells on this half of the region
                int NumCellsOnEachSide = ThisMesh.RegionMeshCount / 2; // Already validated to be an even #
                //'calculate geometric series
                Real64 SummationTerm = 0.0;
                for (int I = 1; I <= NumCellsOnEachSide; ++I) {
                    SummationTerm += std::pow(ThisMesh.GeometricSeriesCoefficient, I - 1);
                }
                //'set up a list of cell widths for this region
                Real64 CellWidth = (GridWidth / 2) / SummationTerm;
                g.CellWidths.push_back(CellWidth);
                for (int I = 1; I <= NumCellsOnEachSide - 1; ++I) {
                    CellWidth *= ThisMesh.GeometricSeriesCoefficient;
                    g.CellWidths.push_back(CellWidth);
                }
                for (int I = NumCellsOnEachSide - 1; I >= 0; --I) {
                    g.CellWidths.push_back(g.CellWidths[I]);
                }

            } else if (ThisMesh.thisMeshDistribution == MeshDistribution::Geometric) {

                int NumCells = ThisMesh.RegionMeshCount;
                if (g.thisRegionType == RegionType::XDirection || g.thisRegionType == RegionType::ZDirection) {
                    //'calculate geometric series
                    Real64 SummationTerm = 0.0;
                    for (int I = 1; I <= NumCells; ++I) {
                        SummationTerm += std::pow(ThisMesh.GeometricSeriesCoefficient, I - 1);
                    }
                    Real64 CellWidth = GridWidth / SummationTerm;
                    if (g.Min == 0) {
                        // Ground region to the left of the slab will have cells expanding to the left
                        // adding them here moving forward, then reversing it to get this effect
                        // Possible spot for diffs
                        g.CellWidths.push_back(CellWidth);
                        for (int I = 0; I < NumCells; ++I) {
                            CellWidth *= ThisMesh.GeometricSeriesCoefficient;
                            g.CellWidths.push_back(CellWidth);
                        }
                        std::reverse(g.CellWidths.begin(), g.CellWidths.end());
                    } else {
                        // Slab region will have cells expanding to the right
                        g.CellWidths.push_back(CellWidth);
                        for (int I = 1; I <= NumCells - 1; ++I) {
                            CellWidth *= ThisMesh.GeometricSeriesCoefficient;
                            g.CellWidths.push_back(CellWidth);
                        }
                    }
                } else if (g.thisRegionType == RegionType::YDirection) {
                    // Assign uniform cell thickness to the slab cells.
                    if (g.Max == this->Extents.yMax) {
                        NumCells = this->NumSlabCells;
                        Real64 CellWidth = GridWidth / NumCells;
                        for (int I = 0; I <= NumCells - 1; ++I) {
                            g.CellWidths.push_back(CellWidth);
                        }
                    } else {
                        //'calculate geometric series
                        Real64 SummationTerm = 0.0;
                        for (int I = 1; I <= NumCells; ++I) {
                            SummationTerm += std::pow(ThisMesh.GeometricSeriesCoefficient, I - 1);
                        }
                        Real64 CellWidth = GridWidth / SummationTerm;
                        // Ground region under the slab will have cells expanding as we go down
                        // adding them here moving forward, then reversing it to get this effect
                        // Possible spot for diffs
                        g.CellWidths.push_back(CellWidth);
                        for (int I = 1; I < NumCells; ++I) {
                            CellWidth *= ThisMesh.GeometricSeriesCoefficient;
                            g.CellWidths.push_back(CellWidth);
                        }
                        std::reverse(g.CellWidths.begin(), g.CellWidths.end());
                    }
                }
            }
        }

        void Domain::PerformIterationLoop(int const CircuitNum) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // Always do start of time step inits
            this->DoStartOfTimeStepInitializations(CircuitNum);

            // Prepare the pipe circuit for calculations, but we'll actually do calcs at the iteration level
            if (this->HasAPipeCircuit) {
                this->PreparePipeCircuitSimulation(CircuitNum);
            }

            // Begin iterating for this time step
            for (int IterationIndex = 1; IterationIndex <= this->SimControls.MaxIterationsPerTS; ++IterationIndex) {

                this->ShiftTemperaturesForNewIteration();

                if (this->HasAPipeCircuit) {
                    this->PerformPipeCircuitSimulation(CircuitNum);
                }

                if (this->DomainNeedsSimulation) this->PerformTemperatureFieldUpdate();
                bool FinishedIterationLoop = false;
                this->DoEndOfIterationOperations(FinishedIterationLoop);

                if (FinishedIterationLoop) break;
            }

            // Update the basement surface temperatures, if any
            if (this->HasBasement || this->HasZoneCoupledBasement) {
                this->UpdateBasementSurfaceTemperatures();
            }

            // Update the slab surface temperatures, if any
            if (this->HasZoneCoupledSlab) {
                this->UpdateZoneSurfaceTemperatures();
            }
        }

        void Domain::PerformTemperatureFieldUpdate() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto &cell(this->Cells(X, Y, Z));

                        switch (cell.cellType) {
                            case CellType::Pipe:
                                //'pipes are simulated separately
                                break;
                            case CellType::GeneralField:
                            case CellType::Slab:
                            case CellType::HorizInsulation:
                            case CellType::VertInsulation:
                                cell.Temperature = this->EvaluateFieldCellTemperature(cell);
                                break;
                            case CellType::GroundSurface:
                                cell.Temperature = this->EvaluateGroundSurfaceTemperature(cell);
                                break;
                            case CellType::FarfieldBoundary:
                                cell.Temperature = this->EvaluateFarfieldBoundaryTemperature(cell);
                                break;
                            case CellType::BasementWall:
                            case CellType::BasementCorner:
                            case CellType::BasementFloor:
                                // basement model, zone-coupled. Call EvaluateZoneInterfaceTemperature routine to handle timestep/hourly simulation.
                                if (this->HasZoneCoupledBasement) {
                                    cell.Temperature = this->EvaluateZoneInterfaceTemperature(cell);
                                } else { // FHX model
                                    cell.Temperature = this->EvaluateBasementCellTemperature(cell);
                                }
                                break;
                            case CellType::ZoneGroundInterface:
                                cell.Temperature = this->EvaluateZoneInterfaceTemperature(cell);
                                break;
                            case CellType::BasementCutaway:
                                // it's ok to not simulate this one
                                break;
                            case CellType::Unknown:
                                assert(false);
                        }
                    }
                }
            }
        }

        Real64 Domain::EvaluateFieldCellTemperature(CartesianCell &cell) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // FUNCTION LOCAL VARIABLE DECLARATIONS:
            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;
            Real64 AdiabaticMultiplier = 1.0;

            // add effect from cell history
            Numerator += cell.Temperature_PrevTimeStep;
            ++Denominator;

            // determine the neighbor types based on cell location
            int NumFieldCells = 0, NumBoundaryCells = 0;
            this->EvaluateCellNeighborDirections(cell, NumFieldCells, NumBoundaryCells);

            // loop across each direction in the simulation
            for (int DirectionCounter = 0; DirectionCounter <= NumFieldCells; ++DirectionCounter) {

                Real64 NeighborTemp = 0.0;
                Real64 Resistance = 0.0;
                Direction CurDirection = this->NeighborFieldCells[DirectionCounter];

                //'evaluate the transient expression terms
                this->EvaluateNeighborCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                      AdiabaticMultiplier);

                Numerator += AdiabaticMultiplier * (cell.Beta / Resistance) * NeighborTemp;
                Denominator += AdiabaticMultiplier * (cell.Beta / Resistance);
            }

            //'now that we have passed all directions, update the temperature
            return Numerator / Denominator;
        }

        Real64 Domain::EvaluateGroundSurfaceTemperature(CartesianCell &cell) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // FUNCTION PARAMETER DEFINITIONS:
            Real64 const AirDensity(1.22521);   // '[kg/m3]
            Real64 const AirSpecificHeat(1003); // '[J/kg-K]
            // evapotranspiration parameters
            Real64 const MeanSolarConstant(0.08196); // 1367 [W/m2], entered in [MJ/m2-minute]
            Real64 const A_s(0.25);                  // ?
            Real64 const B_s(0.5);                   // ?
            Real64 const Absor_Corrected(0.77);
            Real64 const Convert_Wm2_To_MJhrmin(3600.0 / 1000000.0);
            Real64 const Convert_MJhrmin_To_Wm2(1.0 / Convert_Wm2_To_MJhrmin);
            Real64 const Rho_water(998.0); // [kg/m3]

            // FUNCTION LOCAL VARIABLE DECLARATIONS:
            Real64 NeighborTemp = 0.0;
            Real64 IncidentHeatGain;
            Real64 Latitude_Degrees;   // Latitude, degrees N
            Real64 StMeridian_Degrees; // Standard meridian, degrees W -- note it is degrees E in DataEnvironment
            Real64 Longitude_Degrees;  // Longitude, degrees W -- note it is degrees E in DataEnvironment
            // evapotranspiration calculated values
            Real64 Latitude_Radians;
            Real64 DayOfYear;
            Real64 HourOfDay;
            Real64 CurSecondsIntoToday;
            Real64 dr;
            Real64 Declination;
            Real64 b_SC;
            Real64 Sc;
            Real64 Hour_Angle;
            Real64 X_sunset;
            Real64 Sunset_Angle;
            Real64 Solar_Angle_1;
            Real64 Solar_Angle_2;
            Real64 QRAD_A;
            Real64 QRAD_SO;
            Real64 Ratio_SO;
            Real64 IncidentSolar_MJhrmin;
            Real64 AbsorbedIncidentSolar_MJhrmin;
            Real64 VaporPressureSaturated_kPa;
            Real64 VaporPressureActual_kPa;
            Real64 QRAD_NL;
            Real64 NetIncidentRadiation_MJhr; // [MJ/hr]
            Real64 NetIncidentRadiation_Wm2;  // [W/m2]
            Real64 CN;
            Real64 G_hr;
            Real64 Cd;
            Real64 Slope_S;
            Real64 Pressure;
            Real64 PsychrometricConstant;
            Real64 EvapotransFluidLoss_mmhr;
            Real64 EvapotransFluidLoss_mhr;
            Real64 LatentHeatVaporization;
            Real64 EvapotransHeatLoss_MJhrmin; // [MJ/m2-hr]
            Real64 EvapotransHeatLoss_Wm2;     // [W/m2]
            Real64 CurAirTempK;
            Real64 GroundCoverCoefficient;

            // retrieve information from E+ globals
            Latitude_Degrees = DataEnvironment::Latitude;
            StMeridian_Degrees = -DataEnvironment::TimeZoneMeridian; // Standard meridian, degrees W
            Longitude_Degrees = -DataEnvironment::Longitude;         // Longitude, degrees W

            // retrieve any information from input data structure
            GroundCoverCoefficient = this->Moisture.GroundCoverCoefficient;

            // initialize values
            Real64 AdiabaticMultiplier = 1.0;
            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;
            Real64 Resistance = 0.0;
            Real64 Beta = cell.Beta;
            Real64 ThisNormalArea = cell.normalArea(Direction::PositiveY);

            //'add effect from previous time step
            Numerator += cell.Temperature_PrevTimeStep;
            ++Denominator;

            // now that we aren't infinitesimal, we need to determine the neighbor types based on cell location
            int NumFieldCells = 0, NumBoundaryCells = 0;
            this->EvaluateCellNeighborDirections(cell, NumFieldCells, NumBoundaryCells);

            // loop over all regular neighbor cells, check if we have adiabatic on opposite surface
            for (int DirectionCounter = 0; DirectionCounter <= NumFieldCells; ++DirectionCounter) {
                Direction CurDirection = this->NeighborFieldCells[DirectionCounter];

                // Use the multiplier ( either 1 or 2 ) to calculate the neighbor cell effects
                this->EvaluateNeighborCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                      AdiabaticMultiplier);
                Numerator = AdiabaticMultiplier * Numerator + (Beta / Resistance) * NeighborTemp;
                Denominator = AdiabaticMultiplier * Denominator + (Beta / Resistance);
            }

            // do all non-adiabatic boundary types here
            for (int DirectionCounter = 0; DirectionCounter <= NumBoundaryCells; ++DirectionCounter) {
                Direction CurDirection = this->NeighborBoundaryCells[DirectionCounter];

                // For Zone-coupled slab or basement configuration
                if (this->HasZoneCoupledSlab || this->HasZoneCoupledBasement) {
                    //-x-direction will always be a farfield boundary
                    //-z will also be a farfield boundary
                    //+x and +z will be handled above
                    //-y will always be a neighbor cell, so it is handled above
                    //+y will always be the outdoor air
                    if (CurDirection == Direction::NegativeX || CurDirection == Direction::NegativeZ) {
                        // always farfield
                        this->EvaluateFarfieldCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                              AdiabaticMultiplier);
                        Numerator += (Beta / Resistance) * NeighborTemp;
                        Denominator += (Beta / Resistance);
                    } else if (CurDirection == Direction::PositiveY) {
                        // convection at the surface
                        if (DataEnvironment::WindSpeed > 0.1) {
                            Resistance = 208.0 /
                                         (AirDensity * AirSpecificHeat * DataEnvironment::WindSpeed * ThisNormalArea);
                            Numerator += (Beta / Resistance) * this->Cur.CurAirTemp;
                            Denominator += (Beta / Resistance);
                        }
                    } else if (CurDirection == Direction::NegativeY) {
                        assert(false); // debug error, can't get here!
                    }
                } else { // FHX model
                    // x-direction will always be a farfield boundary
                    // z-direction will be handled above -- adiabatic
                    //-y we don't handle here because -y will always be a neighbor cell, so handled above
                    //+y will always be the outdoor air
                    if ((CurDirection == Direction::PositiveX) || (CurDirection == Direction::NegativeX)) {
                        // always farfield
                        this->EvaluateFarfieldCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                              AdiabaticMultiplier);
                        Numerator += (Beta / Resistance) * NeighborTemp;
                        Denominator += (Beta / Resistance);
                    } else if ((CurDirection == Direction::PositiveZ) || (CurDirection == Direction::NegativeZ)) {
                        // debug error, can't get here
                    } else if (CurDirection == Direction::PositiveY) {
                        // convection at the surface
                        if (DataEnvironment::WindSpeed > 0.1) {
                            Resistance = 208.0 /
                                         (AirDensity * AirSpecificHeat * DataEnvironment::WindSpeed * ThisNormalArea);
                            Numerator += (Beta / Resistance) * this->Cur.CurAirTemp;
                            Denominator += (Beta / Resistance);
                        } else {
                            // Future development should include additional natural convection effects here
                        }
                    } else if (CurDirection == Direction::NegativeY) {
                        assert(false); // debug error, can't get here!
                    }
                }
            }

            // Latitude, converted to radians...positive for northern hemisphere, [radians]
            Latitude_Radians = DataGlobals::Pi / 180.0 * Latitude_Degrees;

            // The day of year at this point in the simulation
            DayOfYear = int(this->Cur.CurSimTimeSeconds / DataGlobals::SecsInDay);

            // The number of seconds into the current day
            CurSecondsIntoToday = int(mod(this->Cur.CurSimTimeSeconds, DataGlobals::SecsInDay));

            // The number of hours into today
            HourOfDay = int(CurSecondsIntoToday / DataGlobals::SecInHour);

            // For convenience convert to Kelvin once
            CurAirTempK = this->Cur.CurAirTemp + 273.15;

            // Calculate some angles
            dr = 1.0 + 0.033 * std::cos(2.0 * DataGlobals::Pi * DayOfYear / 365.0);
            Declination = 0.409 * std::sin(2.0 * DataGlobals::Pi / 365.0 * DayOfYear - 1.39);
            b_SC = 2.0 * DataGlobals::Pi * (DayOfYear - 81.0) / 364.0;
            Sc = 0.1645 * std::sin(2.0 * b_SC) - 0.1255 * std::cos(b_SC) - 0.025 * std::sin(b_SC);
            Hour_Angle = DataGlobals::Pi / 12.0 *
                         (((HourOfDay - 0.5) + 0.06667 * (StMeridian_Degrees - Longitude_Degrees) + Sc) - 12.0);

            // Calculate sunset something, and constrain to a minimum of 0.000001
            X_sunset = 1.0 - pow_2(std::tan(Latitude_Radians)) * pow_2(std::tan(Declination));
            X_sunset = max(X_sunset, 0.000001);

            // Find sunset angle
            Sunset_Angle = DataGlobals::Pi / 2.0 -
                           std::atan(-std::tan(Latitude_Radians) * std::tan(Declination) / std::sqrt(X_sunset));

            // Find solar angles
            Solar_Angle_1 = Hour_Angle - DataGlobals::Pi / 24.0;
            Solar_Angle_2 = Hour_Angle + DataGlobals::Pi / 24.0;

            // Constrain solar angles
            if (Solar_Angle_1 < -Sunset_Angle) Solar_Angle_1 = -Sunset_Angle;
            if (Solar_Angle_2 < -Sunset_Angle) Solar_Angle_2 = -Sunset_Angle;
            if (Solar_Angle_1 > Sunset_Angle) Solar_Angle_1 = Sunset_Angle;
            if (Solar_Angle_2 > Sunset_Angle) Solar_Angle_2 = Sunset_Angle;
            if (Solar_Angle_1 > Solar_Angle_2) Solar_Angle_1 = Solar_Angle_2;

            // Convert input solar radiation [w/m2] into units for ET model, [MJ/hr-min]
            IncidentSolar_MJhrmin = this->Cur.CurIncidentSolar * Convert_Wm2_To_MJhrmin;

            // Calculate another Q term...
            QRAD_A = 12.0 * 60.0 / DataGlobals::Pi * MeanSolarConstant * dr *
                     ((Solar_Angle_2 - Solar_Angle_1) * std::sin(Latitude_Radians) * std::sin(Declination) +
                      std::cos(Latitude_Radians) * std::cos(Declination) *
                      (std::sin(Solar_Angle_2) - std::sin(Solar_Angle_1)));

            // Calculate another Q term...
            QRAD_SO = (A_s + B_s + 0.00002 * DataEnvironment::Elevation) * QRAD_A;

            // Correct the Qrad term ... better way??
            if (IncidentSolar_MJhrmin < 0.01) {
                Ratio_SO = 0.0;
            } else {
                if (QRAD_SO != 0.0) {
                    Ratio_SO = IncidentSolar_MJhrmin / QRAD_SO;
                } else {
                    // I used logic below to choose value, divide by 0 = infinity, so value = 1, not sure if correct...
                    Ratio_SO = 1.0;
                }
            }

            // Constrain Ratio_SO
            Ratio_SO = min(Ratio_SO, 1.0);
            Ratio_SO = max(Ratio_SO, 0.3);

            // Calculate another Q term, [MJ/hr-min]
            AbsorbedIncidentSolar_MJhrmin = Absor_Corrected * IncidentSolar_MJhrmin;

            // Calculate saturated vapor pressure, [kPa]
            VaporPressureSaturated_kPa =
                    0.6108 * std::exp(17.27 * this->Cur.CurAirTemp / (this->Cur.CurAirTemp + 237.3));

            // Calculate actual vapor pressure, [kPa]
            VaporPressureActual_kPa = VaporPressureSaturated_kPa * this->Cur.CurRelativeHumidity / 100.0;

            // Calculate another Q term, [MJ/m2-hr]
            QRAD_NL = 2.042E-10 * pow_4(CurAirTempK) * (0.34 - 0.14 * std::sqrt(VaporPressureActual_kPa)) *
                      (1.35 * Ratio_SO - 0.35);

            // Calculate another Q term, [MJ/hr]
            NetIncidentRadiation_MJhr = AbsorbedIncidentSolar_MJhrmin - QRAD_NL;

            // ?
            CN = 37.0;

            // Check whether there was sun
            if (NetIncidentRadiation_MJhr < 0.0) {
                G_hr = 0.5 * NetIncidentRadiation_MJhr;
                Cd = 0.96;
            } else {
                G_hr = 0.1 * NetIncidentRadiation_MJhr;
                Cd = 0.24;
            }

            // Just For Check
            // Lu Xing Sep 22 2009

            Slope_S = 2503.0 * std::exp(17.27 * this->Cur.CurAirTemp / (this->Cur.CurAirTemp + 237.3)) /
                      pow_2(this->Cur.CurAirTemp + 237.3);
            Pressure = 98.0;
            PsychrometricConstant = 0.665e-3 * Pressure;

            // Evapotranspiration constant, [mm/hr]
            EvapotransFluidLoss_mmhr = (GroundCoverCoefficient * Slope_S * (NetIncidentRadiation_MJhr - G_hr) +
                                        PsychrometricConstant * (CN / CurAirTempK) * this->Cur.CurWindSpeed *
                                        (VaporPressureSaturated_kPa - VaporPressureActual_kPa)) /
                                       (Slope_S + PsychrometricConstant * (1 + Cd * this->Cur.CurWindSpeed));

            // Convert units, [m/hr]
            EvapotransFluidLoss_mhr = EvapotransFluidLoss_mmhr / 1000.0;

            // Calculate latent heat, [MJ/kg]
            // Full formulation is cubic: L(T) = -0.0000614342 * T**3 + 0.00158927 * T**2 - 2.36418 * T + 2500.79[5]
            // In: Cubic fit to Table 2.1,p.16, Textbook: R.R.Rogers & M.K. Yau, A Short Course in Cloud Physics, 3e,(1989), Pergamon press
            // But a linear relation should suffice;
            // note-for now using the previous time step temperature as an approximation to help ensure stability
            LatentHeatVaporization = 2.501 - 2.361e-3 * cell.Temperature_PrevTimeStep;

            // Calculate evapotranspiration heat loss, [MJ/m2-hr]
            EvapotransHeatLoss_MJhrmin = Rho_water * EvapotransFluidLoss_mhr * LatentHeatVaporization;

            // Convert net incident solar units, [W/m2]
            NetIncidentRadiation_Wm2 = NetIncidentRadiation_MJhr * Convert_MJhrmin_To_Wm2;

            // Convert evapotranspiration units, [W/m2]
            EvapotransHeatLoss_Wm2 = EvapotransHeatLoss_MJhrmin * Convert_MJhrmin_To_Wm2;

            // Calculate overall net heat ?gain? into the cell, [W]
            IncidentHeatGain = (NetIncidentRadiation_Wm2 - EvapotransHeatLoss_Wm2) * ThisNormalArea;

            // Add any solar/evapotranspiration heat gain here
            Numerator += Beta * IncidentHeatGain;

            // Calculate the return temperature and leave
            return Numerator / Denominator;
        }

        Real64 Domain::EvaluateBasementCellTemperature(CartesianCell &cell) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // FUNCTION LOCAL VARIABLE DECLARATIONS:
            Real64 Beta = 0.0;
            Real64 NeighborTemp = 0.0;
            Real64 HeatFlux;
            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;
            Real64 Resistance = 0.0;
            Real64 AdiabaticMultiplier = 1.0;

            // add effect from previous time step
            Numerator += cell.Temperature_PrevTimeStep;
            ++Denominator;

            switch (cell.cellType) {
                case CellType::BasementWall:

                    // we will only have heat flux from the basement wall and heat conduction to the +x cell

                    // This is actually only a half-cell since the basement wall slices right through the middle in one direction
                    Beta = cell.Beta / 2.0;

                    // get the average basement wall heat flux and add it to the tally
                    HeatFlux = this->GetBasementWallHeatFlux();
                    Numerator += Beta * HeatFlux * cell.height();

                    // then get the +x conduction to continue the heat balance
                    this->EvaluateNeighborCharacteristics(cell, Direction::PositiveX, NeighborTemp, Resistance,
                                                          AdiabaticMultiplier);
                    Numerator += AdiabaticMultiplier * (Beta / Resistance) * NeighborTemp;
                    Denominator += AdiabaticMultiplier * (Beta / Resistance);

                    break;

                case CellType::BasementFloor:

                    // we will only have heat flux from the basement floor and heat conduction to the lower cell

                    // This is actually only a half-cell since the basement wall slices right through the middle in one direction
                    Beta = cell.Beta / 2.0;

                    // get the average basement floor heat flux and add it to the tally
                    HeatFlux = this->GetBasementFloorHeatFlux();
                    Numerator += Beta * HeatFlux * cell.width();

                    // then get the -y conduction to continue the heat balance
                    this->EvaluateNeighborCharacteristics(cell, Direction::NegativeY, NeighborTemp, Resistance,
                                                          AdiabaticMultiplier);
                    Numerator += AdiabaticMultiplier * (Beta / Resistance) * NeighborTemp;
                    Denominator += AdiabaticMultiplier * (Beta / Resistance);

                    break;

                case CellType::BasementCorner:

                    // This is actually only a three-quarter-cell since the basement wall slices right through the middle in both directions
                    Beta = cell.Beta * 3.0 / 4.0;

                    // we will only have heat conduction to the +x and -y cells
                    this->EvaluateNeighborCharacteristics(cell, Direction::PositiveX, NeighborTemp, Resistance,
                                                          AdiabaticMultiplier);
                    Numerator += AdiabaticMultiplier * (Beta / Resistance) * NeighborTemp;
                    Denominator += AdiabaticMultiplier * (Beta / Resistance);

                    this->EvaluateNeighborCharacteristics(cell, Direction::NegativeY, NeighborTemp, Resistance,
                                                          AdiabaticMultiplier);
                    Numerator += AdiabaticMultiplier * (Beta / Resistance) * NeighborTemp;
                    Denominator += AdiabaticMultiplier * (Beta / Resistance);

                    break;

                default:
                    // other cell types are not calculated here
                    break;
            }

            return Numerator / Denominator;
        }

        Real64 Domain::EvaluateFarfieldBoundaryTemperature(CartesianCell &cell) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // FUNCTION LOCAL VARIABLE DECLARATIONS:
            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;
            Real64 Resistance = 0.0;
            Real64 AdiabaticMultiplier = 1.0;
            Real64 Beta = cell.Beta;

            // add effect from previous time step
            Numerator += cell.Temperature_PrevTimeStep;
            ++Denominator;

            // now that we aren't infinitesimal, we need to determine the neighbor types based on cell location
            int NumFieldCells = 0, NumBoundaryCells = 0;
            this->EvaluateCellNeighborDirections(cell, NumFieldCells, NumBoundaryCells);

            // Do all neighbor cells
            for (int DirectionCounter = 0; DirectionCounter <= NumFieldCells; ++DirectionCounter) {
                Direction CurDirection = this->NeighborFieldCells[DirectionCounter];
                Real64 NeighborTemp = 0.0;
                this->EvaluateNeighborCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                      AdiabaticMultiplier);
                Numerator += AdiabaticMultiplier * (Beta / Resistance) * NeighborTemp;
                Denominator += AdiabaticMultiplier * (Beta / Resistance);
            }

            // Then all farfield boundaries
            for (int DirectionCounter = 0; DirectionCounter <= NumBoundaryCells; ++DirectionCounter) {
                Direction CurDirection = this->NeighborBoundaryCells[DirectionCounter];
                Real64 NeighborTemp = 0.0;
                this->EvaluateFarfieldCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                      AdiabaticMultiplier);
                Numerator += AdiabaticMultiplier * (Beta / Resistance) * NeighborTemp;
                Denominator += AdiabaticMultiplier * (Beta / Resistance);
            }

            return Numerator / Denominator;
        }

        Real64 Domain::EvaluateZoneInterfaceTemperature(CartesianCell &cell) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // FUNCTION LOCAL VARIABLE DECLARATIONS:
            Real64 HeatFlux;
            Real64 ConductionArea;
            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;
            Real64 Resistance = 0.0;
            Real64 AdiabaticMultiplier = 1.0;
            Real64 Beta = cell.Beta;

            // add effect from previous time step
            Numerator += cell.Temperature_PrevTimeStep;
            ++Denominator;

            if (cell.cellType == CellType::BasementWall) {
                // Get the average basement wall heat flux and add it to the tally
                HeatFlux = this->WallHeatFlux;
                if (cell.X_index == this->XWallIndex) {
                    ConductionArea = cell.depth() * cell.height();
                    Numerator += Beta * HeatFlux * ConductionArea;
                } else if (cell.Z_index == this->ZWallIndex) {
                    ConductionArea = cell.width() * cell.height();
                    Numerator += Beta * HeatFlux * ConductionArea;
                }
            } else if (cell.cellType == CellType::BasementFloor) {
                // Get the average basement floor heat flux and add it to the tally
                HeatFlux = this->FloorHeatFlux;
                ConductionArea = cell.width() * cell.depth();
                Numerator += Beta * HeatFlux * ConductionArea;
            } else if (cell.cellType == CellType::ZoneGroundInterface) {
                // Get the average slab heat flux and add it to the tally
                HeatFlux = this->WeightedHeatFlux(cell.X_index, cell.Z_index);
                ConductionArea = cell.width() * cell.depth();
                Numerator += Beta * HeatFlux * ConductionArea;
            }

            // determine the neighbor types based on cell location
            int NumFieldCells = 0, NumBoundaryCells = 0;
            this->EvaluateCellNeighborDirections(cell, NumFieldCells, NumBoundaryCells);

            // loop across each direction in the simulation
            for (int DirectionCounter = 0; DirectionCounter <= NumFieldCells; ++DirectionCounter) {

                Real64 NeighborTemp = 0.0;
                Direction CurDirection = this->NeighborFieldCells[DirectionCounter];

                // Have to be careful here to make sure heat conduction happens only in the appropriate directions
                if (cell.cellType == CellType::BasementWall) {
                    // No heat conduction from the X-side basement wall cell to the +x cell ( basement cutaway )
                    if (cell.X_index == this->XWallIndex && CurDirection != Direction::PositiveX) {
                        // Evaluate the transient expression terms
                        this->EvaluateNeighborCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                              AdiabaticMultiplier);
                        Numerator += AdiabaticMultiplier * (Beta / Resistance) * NeighborTemp;
                        Denominator += AdiabaticMultiplier * (Beta / Resistance);
                    }
                    // No heat conduction from the Z-side basement wall cell to the +z cell ( basement cutaway )
                    if (cell.Z_index == this->ZWallIndex && CurDirection != Direction::PositiveZ) {
                        // Evaluate the transient expression terms
                        this->EvaluateNeighborCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                              AdiabaticMultiplier);
                        Numerator += AdiabaticMultiplier * (Beta / Resistance) * NeighborTemp;
                        Denominator += AdiabaticMultiplier * (Beta / Resistance);
                    }
                } else if (cell.cellType == CellType::BasementFloor) {
                    // No heat conduction from the basement floor cell to the +y cell ( basement cutaway )
                    if (CurDirection != Direction::PositiveY) {
                        // Evaluate the transient expression terms
                        this->EvaluateNeighborCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                              AdiabaticMultiplier);
                        Numerator += AdiabaticMultiplier * (Beta / Resistance) * NeighborTemp;
                        Denominator += AdiabaticMultiplier * (Beta / Resistance);
                    }
                } else if (cell.cellType == CellType::ZoneGroundInterface ||
                           cell.cellType == CellType::BasementCorner) {
                    // Heat conduction in all directions
                    // Evaluate the transient expression terms
                    this->EvaluateNeighborCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                          AdiabaticMultiplier);
                    Numerator += AdiabaticMultiplier * (Beta / Resistance) * NeighborTemp;
                    Denominator += AdiabaticMultiplier * (Beta / Resistance);
                }
            }

            return Numerator / Denominator;
        }

        Real64 Domain::GetBasementWallHeatFlux() {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            Real64 RunningSummation = 0.0;
            auto const &numSurfaces = static_cast<unsigned int>(this->BasementZone.WallSurfacePointers.size());
            for (auto &surfaceIndex : this->BasementZone.WallSurfacePointers) {
                RunningSummation += DataHeatBalSurface::QdotConvOutRepPerArea(surfaceIndex);
            }
            return -RunningSummation / numSurfaces; // heat flux is negative here
        }

        Real64 Domain::GetBasementFloorHeatFlux() {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            Real64 RunningSummation = 0.0;
            auto const &numSurfaces = static_cast<unsigned int>(this->BasementZone.FloorSurfacePointers.size());
            for (auto &surfaceIndex : this->BasementZone.FloorSurfacePointers) {
                RunningSummation += DataHeatBalSurface::QdotConvOutRepPerArea(surfaceIndex);
            }
            return -RunningSummation / numSurfaces; // heat flux is negative here
        }

        void Domain::UpdateBasementSurfaceTemperatures() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            Real64 const BigNumber(10000.0);

            // First the wall
            this->BasementWallTemp = this->GetAverageTempByType(CellType::BasementWall);
            int OSCMIndex = this->BasementZone.WallBoundaryOSCMIndex;
            DataSurfaces::OSCM(OSCMIndex).TConv = this->BasementWallTemp;
            DataSurfaces::OSCM(OSCMIndex).HConv = BigNumber;
            DataSurfaces::OSCM(OSCMIndex).TRad = this->BasementWallTemp;
            DataSurfaces::OSCM(OSCMIndex).HRad = 0.0;

            // Then the floor
            this->BasementFloorTemp = this->GetAverageTempByType(CellType::BasementFloor);
            OSCMIndex = this->BasementZone.FloorBoundaryOSCMIndex;
            DataSurfaces::OSCM(OSCMIndex).TConv = this->BasementFloorTemp;
            DataSurfaces::OSCM(OSCMIndex).HConv = BigNumber;
            DataSurfaces::OSCM(OSCMIndex).TRad = this->BasementFloorTemp;
            DataSurfaces::OSCM(OSCMIndex).HRad = 0.0;
        }

        Real64 Domain::GetZoneInterfaceHeatFlux() {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            Real64 RunningSummation = 0.0;
            auto const &NumSurfaces = this->ZoneCoupledSurfaces.size();
            for (auto &z : this->ZoneCoupledSurfaces) {
                RunningSummation += DataHeatBalSurface::QdotConvOutRepPerArea(z.IndexInSurfaceArray);
            }
            return -RunningSummation / NumSurfaces; // heat flux is negative here
        }

        void Domain::UpdateZoneSurfaceTemperatures() {

            // SUBROUTINE INFORMATION:
            //       AUTHOR
            //       DATE WRITTEN
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            Real64 const BigNumber(10000.0);

            this->ZoneCoupledSurfaceTemp = this->GetAverageTempByType(CellType::ZoneGroundInterface);
            int OSCMIndex = this->ZoneCoupledOSCMIndex;
            DataSurfaces::OSCM(OSCMIndex).TConv = this->ZoneCoupledSurfaceTemp;
            DataSurfaces::OSCM(OSCMIndex).HConv = BigNumber;
            DataSurfaces::OSCM(OSCMIndex).TRad = this->ZoneCoupledSurfaceTemp;
            DataSurfaces::OSCM(OSCMIndex).HRad = 0.0;

            // Reset the interface heat flux after iteration
            this->ResetHeatFluxFlag = true;
        }

        Real64 Domain::GetAverageTempByType(CellType const cellType) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // FUNCTION LOCAL VARIABLE DECLARATIONS:
            Real64 RunningSummation = 0.0;
            Real64 RunningVolume = 0.0;

            auto const &cells(Cells);
            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto const &cell(cells(X, Y, Z));
                        if (cell.cellType == cellType) {
                            Real64 CellVolume = cell.volume();
                            RunningVolume += CellVolume;
                            RunningSummation += CellVolume * cell.Temperature;
                        }
                    }
                }
            }

            if (RunningVolume <= 0.0) {
                ShowFatalError("Domain::GetAverageTempByType calculated zero volume, program aborts");
            }

            return RunningSummation / RunningVolume;
        }

        void
        Domain::EvaluateFarfieldCharacteristics(CartesianCell &cell, Direction const direction, Real64 &neighbortemp,
                                                Real64 &resistance, Real64 &adiabaticMultiplier) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 distance(0.0);

            switch (direction) {
                case Direction::NegativeX:
                case Direction::PositiveX:
                    distance = (cell.width() / 2.0);
                    break;
                case Direction::NegativeY:
                case Direction::PositiveY:
                    distance = (cell.height() / 2.0);
                    break;
                case Direction::NegativeZ:
                case Direction::PositiveZ:
                    distance = (cell.depth() / 2.0);
                    break;
            }

            resistance = (distance / 2.0) / (cell.Properties.Conductivity * cell.normalArea(direction));
            neighbortemp = this->GetFarfieldTemp(cell);

            adiabaticMultiplier = cell.NeighborInfo[direction].adiabaticMultiplier;
        }

        Real64 Domain::GetFarfieldTemp(CartesianCell const &cell) {

            // FUNCTION INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            Real64 CurTime = this->Cur.CurSimTimeSeconds;
            Real64 z = this->Extents.yMax - cell.Centroid.Y;
            return this->Farfield.groundTempModel->getGroundTempAtTimeInSeconds(z, CurTime);
        }

        void Domain::PreparePipeCircuitSimulation(int const CircuitNum) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE ARGUMENT DEFINITIONS:
            Real64 const StagnantFluidConvCoeff(200.0);

            auto &thisCircuit = circuits[CircuitNum];

            // Setup circuit flow conditions -- convection coefficient
            int const CellX = thisCircuit.CircuitInletCell.X;
            int const CellY = thisCircuit.CircuitInletCell.Y;
            int const CellZ = thisCircuit.CircuitInletCell.Z;

            // Look up current fluid properties
            Real64 const Density = thisCircuit.CurFluidPropertySet.Density;
            Real64 const Viscosity = thisCircuit.CurFluidPropertySet.Viscosity;
            Real64 const Conductivity = thisCircuit.CurFluidPropertySet.Conductivity;
            Real64 const Prandtl = thisCircuit.CurFluidPropertySet.Prandtl;

            // Flow calculations
            Real64 const Area_c = (DataGlobals::Pi / 4.0) * pow_2(thisCircuit.PipeSize.InnerDia);
            Real64 const Velocity = thisCircuit.CurCircuitFlowRate / (Density * Area_c);

            // Determine convection coefficient based on flow conditions
            Real64 ConvCoefficient;
            if (Velocity > 0) {
                Real64 Reynolds = Density * thisCircuit.PipeSize.InnerDia * Velocity / Viscosity;
                Real64 ExponentTerm;
                if (this->Cells(CellX, CellY, CellZ).PipeCellData.Fluid.Temperature >
                    this->Cells(CellX, CellY, CellZ).PipeCellData.Pipe.Temperature) {
                    ExponentTerm = 0.3;
                } else {
                    ExponentTerm = 0.4;
                }
                Real64 const Nusselt = 0.023 * std::pow(Reynolds, 4.0 / 5.0) * std::pow(Prandtl, ExponentTerm);
                ConvCoefficient = Nusselt * Conductivity / thisCircuit.PipeSize.InnerDia;
            } else {
                ConvCoefficient = StagnantFluidConvCoeff;
            }

            // Assign the convection coefficient
            thisCircuit.CurCircuitConvectionCoefficient = ConvCoefficient;
        }

        void Domain::PerformPipeCircuitSimulation(int const CircuitNum) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 CircuitCrossTemp = 0.0;

            auto &thisCircuit = circuits[CircuitNum];

            // retrieve initial conditions from the data structure
            // these have been set either by the init routine or by the heat pump routine
            Real64 const FlowRate = thisCircuit.CurCircuitFlowRate;
            Real64 const EnteringTemp = thisCircuit.CurCircuitInletTemp;

            // initialize
            int SegmentCellCtr = 0;
            unsigned long const NumSegments = thisCircuit.pipeSegments.size();
            unsigned long segmentNum = 0;

            //'loop across all segments (pipes) of the circuit
            auto &cells(this->Cells);
            for (auto &segment : thisCircuit.pipeSegments) {

                segmentNum++;
                int StartingZ = 0;
                int EndingZ = 0;
                int Increment = 0;

                //'set simulation flow direction
                switch (segment->FlowDirection) {
                    case SegmentFlow::IncreasingZ:
                        StartingZ = 0;
                        EndingZ = this->z_max_index;
                        Increment = 1;
                        break;
                    case SegmentFlow::DecreasingZ:
                        StartingZ = this->z_max_index;
                        EndingZ = 0;
                        Increment = -1;
                        break;
                    default:
                        ShowFatalError("Debug error: invalid flow direction on piping system segment");
                }

                //'find the cell we are working on in order to retrieve cell and neighbor information
                int PipeX = segment->PipeCellCoordinates.X;
                int PipeY = segment->PipeCellCoordinates.Y;

                //'loop across all z-direction indeces
                int const Zindex_stop(floop_end(StartingZ, EndingZ, Increment));
                for (int Zindex = StartingZ; Zindex != Zindex_stop; Zindex += Increment) {

                    //'overall cell segment counter
                    ++SegmentCellCtr;

                    if (SegmentCellCtr == 1) {
                        //'we have the very first cell, need to pass in circuiting entering temperature
                        this->PerformPipeCellSimulation(CircuitNum, cells(PipeX, PipeY, Zindex), FlowRate,
                                                        EnteringTemp);
                    } else {
                        //'we don't have the first cell so just normal simulation
                        if (Zindex == EndingZ) {
                            // simulate current cell using upstream as entering conditions
                            this->PerformPipeCellSimulation(CircuitNum,
                                                            cells(PipeX, PipeY, Zindex),
                                                            FlowRate,
                                                            cells(PipeX, PipeY,
                                                                  Zindex - Increment).PipeCellData.Fluid.Temperature);
                            // store this outlet condition to be passed to the next segment
                            CircuitCrossTemp = cells(PipeX, PipeY, Zindex).PipeCellData.Fluid.Temperature;
                        } else if (Zindex == StartingZ) {
                            // we are starting another segment, use the previous cross temperature
                            this->PerformPipeCellSimulation(CircuitNum, cells(PipeX, PipeY, Zindex), FlowRate,
                                                            CircuitCrossTemp);
                        } else {
                            // we are in an interior node, so just get the upstream cell and use the main simulation
                            this->PerformPipeCellSimulation(CircuitNum,
                                                            cells(PipeX, PipeY, Zindex),
                                                            FlowRate,
                                                            cells(PipeX, PipeY,
                                                                  Zindex - Increment).PipeCellData.Fluid.Temperature);
                        }
                    }

                    // Bookkeeping: segment fluid temperature updates
                    if (Zindex == StartingZ) {
                        if (segmentNum == 1) {
                            segment->InletTemperature = EnteringTemp;
                        } else {
                            segment->InletTemperature = CircuitCrossTemp;
                        }
                    } else if (Zindex == EndingZ) {
                        segment->OutletTemperature = cells(PipeX, PipeY, Zindex).PipeCellData.Fluid.Temperature;
                        segment->FluidHeatLoss = FlowRate * thisCircuit.CurFluidPropertySet.SpecificHeat *
                                                 (segment->InletTemperature - segment->OutletTemperature);
                    }

                    // Bookkeeping: circuit fluid temperature updates
                    if ((segmentNum == 1) && (Zindex == StartingZ)) {
                        thisCircuit.InletTemperature = EnteringTemp;
                    } else if ((segmentNum == NumSegments) && (Zindex == EndingZ)) {
                        thisCircuit.OutletTemperature = cells(PipeX, PipeY, Zindex).PipeCellData.Fluid.Temperature;
                        thisCircuit.FluidHeatLoss =
                                FlowRate * thisCircuit.CurFluidPropertySet.SpecificHeat *
                                (thisCircuit.InletTemperature - thisCircuit.OutletTemperature);
                    }
                }
            }
        }

        void
        Domain::PerformPipeCellSimulation(int const CircuitNum, CartesianCell &ThisCell, Real64 const FlowRate,
                                          Real64 const EnteringTemp) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            auto &thisCircuit = circuits[CircuitNum];

            for (int Iter = 1; Iter <= thisCircuit.MaxIterationsPerTS; ++Iter) {

                //'shift all the pipe related temperatures for the next internal pipe iteration
                ShiftPipeTemperaturesForNewIteration(ThisCell);

                //'simulate the funny interface soil cell between the radial and cartesian systems
                this->SimulateRadialToCartesianInterface(ThisCell);

                //'simulate the outermost radial slice
                SimulateOuterMostRadialSoilSlice(CircuitNum, ThisCell);

                //'we only need to simulate these if they actually exist!
                if (!ThisCell.PipeCellData.Soil.empty()) {

                    //'simulate all interior radial slices
                    SimulateAllInteriorRadialSoilSlices(ThisCell);

                    //'simulate the innermost radial soil slice
                    SimulateInnerMostRadialSoilSlice(CircuitNum, ThisCell);
                }

                if (thisCircuit.HasInsulation) {
                    SimulateRadialInsulationCell(ThisCell);
                }

                //'simulate the pipe cell
                SimulateRadialPipeCell(CircuitNum, ThisCell, thisCircuit.CurCircuitConvectionCoefficient);

                //'simulate the water cell
                SimulateFluidCell(ThisCell, FlowRate, thisCircuit.CurCircuitConvectionCoefficient, EnteringTemp);

                //'check convergence
                if (IsConverged_PipeCurrentToPrevIteration(CircuitNum, ThisCell)) break; // potential diff source
            }
        }

        void Domain::SimulateRadialToCartesianInterface(CartesianCell &cell) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            static std::vector<Direction> const Directions = {Direction::NegativeX, Direction::NegativeY,
                                                              Direction::PositiveX, Direction::PositiveY};

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;

            //'add effects from this cell history
            Numerator += cell.Temperature_PrevTimeStep;
            ++Denominator;

            //'add effects from outermost radial cell
            auto &outerRadialCell = cell.PipeCellData.Soil.back();
            Real64 OutermostRadialCellOuterRadius = outerRadialCell.OuterRadius;
            Real64 OutermostRadialCellRadialCentroid = outerRadialCell.RadialCentroid;
            Real64 OutermostRadialCellTemperature = outerRadialCell.Temperature;
            Real64 Resistance = std::log(OutermostRadialCellOuterRadius / OutermostRadialCellRadialCentroid) /
                                (2.0 * DataGlobals::Pi * cell.depth() * cell.Properties.Conductivity);
            Numerator += (cell.Beta / Resistance) * OutermostRadialCellTemperature;
            Denominator += (cell.Beta / Resistance);

            //'add effects from neighboring Cartesian cells
            for (auto &curDirection : Directions) {
                Real64 AdiabaticMultiplier = 1.0;
                Real64 NeighborTemp = 0.0;
                this->EvaluateNeighborCharacteristics(cell, curDirection, NeighborTemp, Resistance,
                                                      AdiabaticMultiplier);
                Numerator += AdiabaticMultiplier * (cell.Beta / Resistance) * NeighborTemp;
                Denominator += AdiabaticMultiplier * (cell.Beta / Resistance);
            }

            //'calculate the new temperature
            cell.Temperature = Numerator / Denominator;
        }

        void SimulateOuterMostRadialSoilSlice(int const CircuitNum, CartesianCell &cell) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 NextOuterRadialCellOuterRadius;
            Real64 NextOuterRadialCellRadialCentroid;
            Real64 NextOuterRadialCellConductivity;
            Real64 NextOuterRadialCellTemperature;

            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;

            //'convenience variables
            int const numSoilCells = static_cast<int>(cell.PipeCellData.Soil.size());
            auto &outerMostSoilCell = cell.PipeCellData.Soil[numSoilCells - 1];
            Real64 ThisRadialCellOuterRadius = outerMostSoilCell.OuterRadius;
            Real64 ThisRadialCellRadialCentroid = outerMostSoilCell.RadialCentroid;
            Real64 ThisRadialCellConductivity = outerMostSoilCell.Properties.Conductivity;
            Real64 ThisRadialCellInnerRadius = outerMostSoilCell.InnerRadius;
            Real64 ThisRadialCellTemperature_PrevTimeStep = outerMostSoilCell.Temperature_PrevTimeStep;
            if (numSoilCells == 1) {
                if (circuits[CircuitNum].HasInsulation) {
                    NextOuterRadialCellOuterRadius = cell.PipeCellData.Insulation.OuterRadius;
                    NextOuterRadialCellRadialCentroid = cell.PipeCellData.Insulation.RadialCentroid;
                    NextOuterRadialCellConductivity = cell.PipeCellData.Insulation.Properties.Conductivity;
                    NextOuterRadialCellTemperature = cell.PipeCellData.Insulation.Temperature;
                } else {
                    NextOuterRadialCellOuterRadius = cell.PipeCellData.Pipe.OuterRadius;
                    NextOuterRadialCellRadialCentroid = cell.PipeCellData.Pipe.RadialCentroid;
                    NextOuterRadialCellConductivity = cell.PipeCellData.Pipe.Properties.Conductivity;
                    NextOuterRadialCellTemperature = cell.PipeCellData.Pipe.Temperature;
                }
            } else {
                auto &nextOuterMostSoilCell = cell.PipeCellData.Soil[numSoilCells - 2];
                NextOuterRadialCellOuterRadius = nextOuterMostSoilCell.OuterRadius;
                NextOuterRadialCellRadialCentroid = nextOuterMostSoilCell.RadialCentroid;
                NextOuterRadialCellConductivity = nextOuterMostSoilCell.Properties.Conductivity;
                NextOuterRadialCellTemperature = nextOuterMostSoilCell.Temperature;
            }

            //'any broadly defined variables
            Real64 Beta = outerMostSoilCell.Beta;

            //'add effects from this cell history
            Numerator += ThisRadialCellTemperature_PrevTimeStep;
            ++Denominator;

            //'add effects from interface cell
            Real64 Resistance = std::log(ThisRadialCellOuterRadius / ThisRadialCellRadialCentroid) /
                                (2 * DataGlobals::Pi * cell.depth() * ThisRadialCellConductivity);
            Numerator += (Beta / Resistance) * cell.Temperature;
            Denominator += (Beta / Resistance);

            //'add effects from inner radial cell
            Resistance = (std::log(ThisRadialCellRadialCentroid / ThisRadialCellInnerRadius) /
                          (2 * DataGlobals::Pi * cell.depth() * ThisRadialCellConductivity)) +
                         (std::log(NextOuterRadialCellOuterRadius / NextOuterRadialCellRadialCentroid) /
                          (2 * DataGlobals::Pi * cell.depth() * NextOuterRadialCellConductivity));
            Numerator += (Beta / Resistance) * NextOuterRadialCellTemperature;
            Denominator += (Beta / Resistance);

            //'calculate the new temperature
            outerMostSoilCell.Temperature = Numerator / Denominator;
        }

        void SimulateAllInteriorRadialSoilSlices(CartesianCell &cell) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            for (int rCtr = (int) cell.PipeCellData.Soil.size() - 2; rCtr >= 1; --rCtr) {

                Real64 Numerator = 0.0;
                Real64 Denominator = 0.0;

                //'convenience variables
                auto &thisSoilCell = cell.PipeCellData.Soil[rCtr];
                Real64 ThisRadialCellOuterRadius = thisSoilCell.OuterRadius;
                Real64 ThisRadialCellRadialCentroid = thisSoilCell.RadialCentroid;
                Real64 ThisRadialCellConductivity = thisSoilCell.Properties.Conductivity;
                Real64 ThisRadialCellInnerRadius = thisSoilCell.InnerRadius;
                Real64 ThisRadialCellTemperature_PrevTimeStep = thisSoilCell.Temperature_PrevTimeStep;

                auto const &minusSoilCell = cell.PipeCellData.Soil[rCtr - 1];
                Real64 InnerRadialCellOuterRadius = minusSoilCell.OuterRadius;
                Real64 InnerRadialCellRadialCentroid = minusSoilCell.RadialCentroid;
                Real64 InnerRadialCellConductivity = minusSoilCell.Properties.Conductivity;
                Real64 InnerRadialCellTemperature = minusSoilCell.Temperature;

                auto const &plusSoilCell = cell.PipeCellData.Soil[rCtr + 1];
                Real64 OuterRadialCellRadialCentroid = plusSoilCell.RadialCentroid;
                Real64 OuterRadialCellConductivity = plusSoilCell.Properties.Conductivity;
                Real64 OuterRadialCellInnerRadius = plusSoilCell.InnerRadius;
                Real64 OuterRadialCellTemperature = plusSoilCell.Temperature;

                //'add effects from this cell history
                Numerator += ThisRadialCellTemperature_PrevTimeStep;
                ++Denominator;

                //'add effects from outer cell
                Real64 Resistance =
                        (std::log(OuterRadialCellRadialCentroid / OuterRadialCellInnerRadius) /
                         (2 * DataGlobals::Pi * cell.depth() * OuterRadialCellConductivity)) +
                        (std::log(ThisRadialCellOuterRadius / ThisRadialCellRadialCentroid) /
                         (2 * DataGlobals::Pi * cell.depth() * ThisRadialCellConductivity));
                Numerator += (thisSoilCell.Beta / Resistance) * OuterRadialCellTemperature;
                Denominator += (thisSoilCell.Beta / Resistance);

                //'add effects from inner cell
                Resistance =
                        (std::log(ThisRadialCellRadialCentroid / ThisRadialCellInnerRadius) /
                         (2 * DataGlobals::Pi * cell.depth() * ThisRadialCellConductivity)) +
                        (std::log(InnerRadialCellOuterRadius / InnerRadialCellRadialCentroid) /
                         (2 * DataGlobals::Pi * cell.depth() * InnerRadialCellConductivity));
                Numerator += (thisSoilCell.Beta / Resistance) * InnerRadialCellTemperature;
                Denominator += (thisSoilCell.Beta / Resistance);

                //'calculate the new temperature
                thisSoilCell.Temperature = Numerator / Denominator;
            }
        }

        void SimulateInnerMostRadialSoilSlice(int const CircuitNum, CartesianCell &cell) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 Resistance;
            Real64 InnerNeighborRadialCellOuterRadius;
            Real64 InnerNeighborRadialCellRadialCentroid;
            Real64 InnerNeighborRadialCellConductivity;
            Real64 InnerNeighborRadialCellTemperature;

            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;

            //'convenience variables
            if (circuits[CircuitNum].HasInsulation) {
                InnerNeighborRadialCellOuterRadius = cell.PipeCellData.Insulation.OuterRadius;
                InnerNeighborRadialCellRadialCentroid = cell.PipeCellData.Insulation.RadialCentroid;
                InnerNeighborRadialCellConductivity = cell.PipeCellData.Insulation.Properties.Conductivity;
                InnerNeighborRadialCellTemperature = cell.PipeCellData.Insulation.Temperature;
            } else {
                InnerNeighborRadialCellOuterRadius = cell.PipeCellData.Pipe.OuterRadius;
                InnerNeighborRadialCellRadialCentroid = cell.PipeCellData.Pipe.RadialCentroid;
                InnerNeighborRadialCellConductivity = cell.PipeCellData.Pipe.Properties.Conductivity;
                InnerNeighborRadialCellTemperature = cell.PipeCellData.Pipe.Temperature;
            }

            auto &soilZero = cell.PipeCellData.Soil[0];
            Real64 ThisRadialCellOuterRadius = soilZero.OuterRadius;
            Real64 ThisRadialCellRadialCentroid = soilZero.RadialCentroid;
            Real64 ThisRadialCellConductivity = soilZero.Properties.Conductivity;
            Real64 ThisRadialCellInnerRadius = soilZero.InnerRadius;
            Real64 ThisRadialCellTemperature_PrevTimeStep = soilZero.Temperature_PrevTimeStep;

            auto const &soilOne = cell.PipeCellData.Soil[1];
            Real64 OuterNeighborRadialCellRadialCentroid = soilOne.RadialCentroid;
            Real64 OuterNeighborRadialCellConductivity = soilOne.Properties.Conductivity;
            Real64 OuterNeighborRadialCellInnerRadius = soilOne.InnerRadius;
            Real64 OuterNeighborRadialCellTemperature = soilOne.Temperature;

            //'add effects from this cell history
            Numerator += ThisRadialCellTemperature_PrevTimeStep;
            ++Denominator;

            //'add effects from outer radial cell
            Resistance = (std::log(OuterNeighborRadialCellRadialCentroid / OuterNeighborRadialCellInnerRadius) /
                          (2 * DataGlobals::Pi * cell.depth() * OuterNeighborRadialCellConductivity)) +
                         (std::log(ThisRadialCellOuterRadius / ThisRadialCellRadialCentroid) /
                          (2 * DataGlobals::Pi * cell.depth() * ThisRadialCellConductivity));
            Numerator += (soilZero.Beta / Resistance) * OuterNeighborRadialCellTemperature;
            Denominator += (soilZero.Beta / Resistance);

            //'add effects from pipe cell
            Resistance = (std::log(ThisRadialCellRadialCentroid / ThisRadialCellInnerRadius) /
                          (2 * DataGlobals::Pi * cell.depth() * ThisRadialCellConductivity)) +
                         (std::log(InnerNeighborRadialCellOuterRadius / InnerNeighborRadialCellRadialCentroid) /
                          (2 * DataGlobals::Pi * cell.depth() * InnerNeighborRadialCellConductivity));
            Numerator += (soilZero.Beta / Resistance) * InnerNeighborRadialCellTemperature;
            Denominator += (soilZero.Beta / Resistance);

            //'calculate the new temperature
            soilZero.Temperature = Numerator / Denominator;
        }

        void SimulateRadialInsulationCell(CartesianCell &cell) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;

            //'convenience variables
            auto const &PipeCell = cell.PipeCellData.Pipe;
            auto const &ThisInsulationCell = cell.PipeCellData.Insulation;
            auto const &NextInnerRadialCell = cell.PipeCellData.Soil[0];

            //'add effects from this cell history
            Numerator += ThisInsulationCell.Temperature_PrevTimeStep;
            ++Denominator;

            //'add effects from outer radial cell
            Real64 Resistance = (std::log(NextInnerRadialCell.RadialCentroid / NextInnerRadialCell.InnerRadius) /
                                 (2 * DataGlobals::Pi * cell.depth() * NextInnerRadialCell.Properties.Conductivity)) +
                                (std::log(ThisInsulationCell.OuterRadius / ThisInsulationCell.RadialCentroid) /
                                 (2 * DataGlobals::Pi * cell.depth() * ThisInsulationCell.Properties.Conductivity));
            Numerator += (ThisInsulationCell.Beta / Resistance) * NextInnerRadialCell.Temperature;
            Denominator += (ThisInsulationCell.Beta / Resistance);

            //'add effects from pipe cell
            Resistance = (std::log(ThisInsulationCell.RadialCentroid / ThisInsulationCell.InnerRadius) /
                          (2 * DataGlobals::Pi * cell.depth() * ThisInsulationCell.Properties.Conductivity)) +
                         (std::log(PipeCell.OuterRadius / PipeCell.RadialCentroid) /
                          (2 * DataGlobals::Pi * cell.depth() * PipeCell.Properties.Conductivity));
            Numerator += (ThisInsulationCell.Beta / Resistance) * PipeCell.Temperature;
            Denominator += (ThisInsulationCell.Beta / Resistance);

            //'calculate the new temperature
            cell.PipeCellData.Insulation.Temperature = Numerator / Denominator;
        }

        void SimulateRadialPipeCell(int const CircuitNum, CartesianCell &cell, Real64 const ConvectionCoefficient) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 OuterNeighborRadialCellRadialCentroid;
            Real64 OuterNeighborRadialCellConductivity;
            Real64 OuterNeighborRadialCellInnerRadius;
            Real64 OuterNeighborRadialCellTemperature;

            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;

            //'convenience variables
            if (circuits[CircuitNum].HasInsulation) {
                OuterNeighborRadialCellRadialCentroid = cell.PipeCellData.Insulation.RadialCentroid;
                OuterNeighborRadialCellConductivity = cell.PipeCellData.Insulation.Properties.Conductivity;
                OuterNeighborRadialCellInnerRadius = cell.PipeCellData.Insulation.InnerRadius;
                OuterNeighborRadialCellTemperature = cell.PipeCellData.Insulation.Temperature;
            } else {
                auto const &soilZero = cell.PipeCellData.Soil[0];
                OuterNeighborRadialCellRadialCentroid = soilZero.RadialCentroid;
                OuterNeighborRadialCellConductivity = soilZero.Properties.Conductivity;
                OuterNeighborRadialCellInnerRadius = soilZero.InnerRadius;
                OuterNeighborRadialCellTemperature = soilZero.Temperature;
            }

            Real64 const ThisPipeCellOuterRadius = cell.PipeCellData.Pipe.OuterRadius;
            Real64 const ThisPipeCellRadialCentroid = cell.PipeCellData.Pipe.RadialCentroid;
            Real64 const ThisPipeCellConductivity = cell.PipeCellData.Pipe.Properties.Conductivity;
            Real64 const ThisPipeCellInnerRadius = cell.PipeCellData.Pipe.InnerRadius;
            Real64 const ThisPipeCellTemperature_PrevTimeStep = cell.PipeCellData.Pipe.Temperature_PrevTimeStep;

            Real64 const FluidCellTemperature = cell.PipeCellData.Fluid.Temperature;

            //'add effects from this cell history
            Numerator += ThisPipeCellTemperature_PrevTimeStep;
            ++Denominator;

            //'add effects from outer radial cell
            Real64 Resistance = (std::log(OuterNeighborRadialCellRadialCentroid / OuterNeighborRadialCellInnerRadius) /
                                 (2 * DataGlobals::Pi * cell.depth() * OuterNeighborRadialCellConductivity)) +
                                (std::log(ThisPipeCellOuterRadius / ThisPipeCellRadialCentroid) /
                                 (2 * DataGlobals::Pi * cell.depth() * ThisPipeCellConductivity));
            Numerator += (cell.PipeCellData.Pipe.Beta / Resistance) * OuterNeighborRadialCellTemperature;
            Denominator += (cell.PipeCellData.Pipe.Beta / Resistance);

            //'add effects from water cell
            Real64 PipeConductionResistance =
                    std::log(ThisPipeCellRadialCentroid / ThisPipeCellInnerRadius) /
                    (2 * DataGlobals::Pi * cell.depth() * ThisPipeCellConductivity);
            Real64 ConvectiveResistance =
                    1.0 / (ConvectionCoefficient * 2 * DataGlobals::Pi * ThisPipeCellInnerRadius * cell.depth());
            Resistance = PipeConductionResistance + ConvectiveResistance;
            Numerator += (cell.PipeCellData.Pipe.Beta / Resistance) * FluidCellTemperature;
            Denominator += (cell.PipeCellData.Pipe.Beta / Resistance);

            //'calculate new temperature
            cell.PipeCellData.Pipe.Temperature = Numerator / Denominator;
        }

        void SimulateFluidCell(CartesianCell &cell, Real64 const FlowRate, Real64 const ConvectionCoefficient,
                               Real64 const EnteringFluidTemp) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            Real64 Numerator = 0.0;
            Real64 Denominator = 0.0;

            //'convenience variables
            Real64 const FluidCellTemperature_PrevTimeStep = cell.PipeCellData.Fluid.Temperature_PrevTimeStep;
            Real64 const FluidCellSpecificHeat = cell.PipeCellData.Fluid.Properties.SpecificHeat;

            Real64 const PipeCellRadialCentroid = cell.PipeCellData.Pipe.RadialCentroid;
            Real64 const PipeCellConductivity = cell.PipeCellData.Pipe.Properties.Conductivity;
            Real64 const PipeCellInnerRadius = cell.PipeCellData.Pipe.InnerRadius;
            Real64 const PipeCellTemperature = cell.PipeCellData.Pipe.Temperature;

            //'add effects from this cell history
            Numerator += FluidCellTemperature_PrevTimeStep;
            ++Denominator;

            //'add effects from outer pipe cell
            Real64 PipeConductionResistance = std::log(PipeCellRadialCentroid / PipeCellInnerRadius) /
                                              (2 * DataGlobals::Pi * cell.depth() * PipeCellConductivity);
            Real64 ConvectiveResistance =
                    1.0 / (ConvectionCoefficient * 2 * DataGlobals::Pi * PipeCellInnerRadius * cell.depth());
            Real64 TotalPipeResistance = PipeConductionResistance + ConvectiveResistance;
            Numerator += (cell.PipeCellData.Fluid.Beta / TotalPipeResistance) * PipeCellTemperature;
            Denominator += (cell.PipeCellData.Fluid.Beta / TotalPipeResistance);

            //'add effects from upstream flow
            if (FlowRate > 0.0) {
                Real64 UpstreamResistance = 1 / (FlowRate * FluidCellSpecificHeat);
                Numerator += (cell.PipeCellData.Fluid.Beta / UpstreamResistance) * EnteringFluidTemp;
                Denominator += (cell.PipeCellData.Fluid.Beta / UpstreamResistance);
            }

            //'calculate new temperature
            cell.PipeCellData.Fluid.Temperature = Numerator / Denominator;
        }

        void Domain::DoOneTimeInitializations(int const CircuitNum) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            //'initialize cell properties
            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto &cell(this->Cells(X, Y, Z));
                        switch (cell.cellType) {
                            case CellType::Pipe:
                                cell.Properties = this->GroundProperties;
                                for (auto &soilCell : cell.PipeCellData.Soil) {
                                    soilCell.Properties = this->GroundProperties;
                                }
                                if (CircuitNum >= 0) {
                                    cell.PipeCellData.Pipe.Properties = circuits[CircuitNum].PipeProperties;
                                    if (circuits[CircuitNum].HasInsulation) {
                                        cell.PipeCellData.Insulation.Properties = circuits[CircuitNum].InsulationProperties;
                                    }
                                }
                                break;
                            case CellType::GeneralField:
                            case CellType::GroundSurface:
                            case CellType::FarfieldBoundary:
                                cell.Properties = this->GroundProperties;
                                break;
                            case CellType::BasementWall:
                            case CellType::BasementFloor:
                            case CellType::BasementCorner:
                                if (this->HasZoneCoupledBasement) { // Basement interface layer
                                    cell.Properties = this->BasementInterfaceProperties;
                                } else { // Basement cells are partially ground, give them some props
                                    cell.Properties = this->GroundProperties;
                                }
                                break;
                            case CellType::Slab:
                                cell.Properties = this->SlabProperties;
                                break;
                            case CellType::HorizInsulation:
                                cell.Properties = this->HorizInsProperties;
                                break;
                            case CellType::VertInsulation:
                                cell.Properties = this->VertInsProperties;
                                break;
                            case CellType::ZoneGroundInterface:
                                if (this->SlabInGradeFlag) {
                                    cell.Properties = this->SlabProperties;
                                } else {
                                    cell.Properties = this->GroundProperties;
                                }
                                break;
                            case CellType::BasementCutaway:
                                break;
                            case CellType::Unknown:
                                assert(false);
                        }
                    }
                }
            }

            //'calculate one-time resistance terms for cartesian cells
            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto &cell(this->Cells(X, Y, Z));
                        int NumFieldCells = 0, NumBoundaryCells = 0;
                        this->EvaluateCellNeighborDirections(cell, NumFieldCells, NumBoundaryCells);
                        for (int DirectionCtr = 0; DirectionCtr <= NumFieldCells; ++DirectionCtr) {
                            Direction CurDirection = this->NeighborFieldCells[DirectionCtr];
                            Real64 AdiabaticMultiplier = 1.0, NeighborTemp = 0.0, Resistance = 0.0;
                            this->EvaluateNeighborCharacteristics(cell, CurDirection, NeighborTemp, Resistance,
                                                                  AdiabaticMultiplier);
                            int NX = 0, NY = 0, NZ = 0;
                            cell.EvaluateNeighborCoordinates(CurDirection, NX, NY, NZ);
                        }
                    }
                }
            }

            //'initialize freezing calculation variables
            this->InitializeSoilMoistureCalcs();

            //'we can also initialize the domain based on the farfield temperature here
            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto &cell(this->Cells(X, Y, Z));

                        // On OneTimeInit, the cur sim time should be zero, so this will be OK
                        Real64 ThisCellTemp = this->GetFarfieldTemp(cell);
                        cell.Temperature = ThisCellTemp;
                        cell.Temperature_PrevIteration = ThisCellTemp;
                        cell.Temperature_PrevTimeStep = ThisCellTemp;

                        if (cell.cellType == CellType::Pipe) {

                            for (auto &soilCell : cell.PipeCellData.Soil) {
                                soilCell.Temperature = ThisCellTemp;
                                soilCell.Temperature_PrevIteration = ThisCellTemp;
                                soilCell.Temperature_PrevTimeStep = ThisCellTemp;
                            }
                            cell.PipeCellData.Pipe.Temperature = ThisCellTemp;
                            cell.PipeCellData.Pipe.Temperature_PrevIteration = ThisCellTemp;
                            cell.PipeCellData.Pipe.Temperature_PrevTimeStep = ThisCellTemp;
                            if (CircuitNum >= 0) {
                                if (circuits[CircuitNum].HasInsulation) {
                                    cell.PipeCellData.Insulation.Temperature = ThisCellTemp;
                                    cell.PipeCellData.Insulation.Temperature_PrevIteration = ThisCellTemp;
                                    cell.PipeCellData.Insulation.Temperature_PrevTimeStep = ThisCellTemp;
                                }
                            }
                            cell.PipeCellData.Fluid.Temperature = ThisCellTemp;
                            cell.PipeCellData.Fluid.Temperature_PrevIteration = ThisCellTemp;
                            cell.PipeCellData.Fluid.Temperature_PrevTimeStep = ThisCellTemp;
                        }
                    }
                }
            }
        }

        void Domain::DoStartOfTimeStepInitializations(int const CircuitNum) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE LOCAL VARIABLE DECLARATIONS:
            static std::string const RoutineName("PipingSystemCircuit::DoStartOfTimeStepInitializations");
            Real64 Beta;
            Real64 CellTemp;
            Real64 CellRhoCp;
            Real64 FluidCp;
            Real64 FluidDensity;
            Real64 FluidConductivity;
            Real64 FluidViscosity;
            Real64 FluidPrandtl;

            // Update environmental conditions
            this->Cur.CurAirTemp = DataEnvironment::OutDryBulbTemp;
            this->Cur.CurWindSpeed = DataEnvironment::WindSpeed;
            this->Cur.CurRelativeHumidity = DataEnvironment::OutRelHum;
            this->Cur.CurIncidentSolar = DataEnvironment::BeamSolarRad;

            // If pipe circuit present
            if (CircuitNum >= 0) {
                auto &thisCircuit = circuits[CircuitNum];
                // retrieve fluid properties based on the circuit inlet temperature -- which varies during the simulation
                // but need to verify the value of inlet temperature during warm up, etc.
                FluidCp = FluidProperties::GetSpecificHeatGlycol(DataPlant::PlantLoop(thisCircuit.LoopNum).FluidName,
                                                                 thisCircuit.InletTemperature,
                                                                 DataPlant::PlantLoop(thisCircuit.LoopNum).FluidIndex,
                                                                 RoutineName);
                FluidDensity = FluidProperties::GetDensityGlycol(DataPlant::PlantLoop(thisCircuit.LoopNum).FluidName,
                                                                 thisCircuit.InletTemperature,
                                                                 DataPlant::PlantLoop(thisCircuit.LoopNum).FluidIndex,
                                                                 RoutineName);
                FluidConductivity = FluidProperties::GetConductivityGlycol(
                        DataPlant::PlantLoop(thisCircuit.LoopNum).FluidName,
                        thisCircuit.InletTemperature,
                        DataPlant::PlantLoop(thisCircuit.LoopNum).FluidIndex,
                        RoutineName);
                FluidViscosity = FluidProperties::GetViscosityGlycol(
                        DataPlant::PlantLoop(thisCircuit.LoopNum).FluidName,
                        thisCircuit.InletTemperature,
                        DataPlant::PlantLoop(thisCircuit.LoopNum).FluidIndex,
                        RoutineName);

                // Doesn't anyone care about poor Ludwig Prandtl?
                FluidPrandtl = 3.0;

                // then assign these fluid properties to the current fluid property set for easy lookup as needed
                thisCircuit.CurFluidPropertySet.Conductivity = FluidConductivity;
                thisCircuit.CurFluidPropertySet.Density = FluidDensity;
                thisCircuit.CurFluidPropertySet.SpecificHeat = FluidCp;
                thisCircuit.CurFluidPropertySet.Viscosity = FluidViscosity;
                thisCircuit.CurFluidPropertySet.Prandtl = FluidPrandtl;
            }

            //'now update cell properties
            auto &cells(this->Cells);
            for (int X = 0, X_end = this->x_max_index; X <= X_end; ++X) {
                for (int Y = 0, Y_end = this->y_max_index; Y <= Y_end; ++Y) {
                    for (int Z = 0, Z_end = this->z_max_index; Z <= Z_end; ++Z) {
                        auto &cell(cells(X, Y, Z));
                        switch (cell.cellType) {
                            case CellType::GeneralField:
                            case CellType::FarfieldBoundary:
                            case CellType::GroundSurface:
                            case CellType::BasementCorner:
                            case CellType::BasementFloor:
                            case CellType::BasementWall:
                                // UPDATE CELL PROPERTY SETS
                                //'main ground cells, update with soil properties
                                CellTemp = cell.Temperature;
                                this->EvaluateSoilRhoCp(CellTemp, CellRhoCp);
                                cell.Properties.SpecificHeat = CellRhoCp / cell.Properties.Density;
                                // UPDATE BETA VALUE
                                //'these are basic cartesian calculation cells
                                Beta = this->Cur.CurSimTimeStepSize /
                                       (cell.Properties.Density * cell.volume() * cell.Properties.SpecificHeat);
                                cell.Beta = Beta;
                                break;
                            case CellType::HorizInsulation:
                            case CellType::VertInsulation:
                            case CellType::Slab:
                            case CellType::ZoneGroundInterface:
                                Beta = this->Cur.CurSimTimeStepSize /
                                       (cell.Properties.Density * cell.volume() * cell.Properties.SpecificHeat);
                                this->Cells(X, Y, Z).Beta = Beta;
                                break;
                            case CellType::Pipe:
                                // If pipe circuit present
                                if (CircuitNum >= 0) {
                                    // UPDATE CELL PROPERTY SETS
                                    //'first update the outer cell itself
                                    CellTemp = cell.Temperature;
                                    this->EvaluateSoilRhoCp(CellTemp, CellRhoCp);
                                    cell.Properties.SpecificHeat = CellRhoCp / cell.Properties.Density;
                                    //'then update all the soil radial cells
                                    for (auto &soilCell : cell.PipeCellData.Soil) {
                                        CellTemp = soilCell.Temperature;
                                        this->EvaluateSoilRhoCp(CellTemp, CellRhoCp);
                                        soilCell.Properties.SpecificHeat = CellRhoCp / soilCell.Properties.Density;
                                    }

                                    // UPDATE BETA VALUES
                                    //'set the interface cell
                                    Beta = this->Cur.CurSimTimeStepSize /
                                           (cell.Properties.Density * cell.PipeCellData.InterfaceVolume *
                                            cell.Properties.SpecificHeat);
                                    cell.Beta = Beta;

                                    //'set the radial soil cells
                                    for (auto &soilCell : cell.PipeCellData.Soil) {
                                        Beta = this->Cur.CurSimTimeStepSize /
                                               (soilCell.Properties.Density * soilCell.XY_CrossSectArea() *
                                                cell.depth() * soilCell.Properties.SpecificHeat);
                                        soilCell.Beta = Beta;
                                    }

                                    //'then insulation if it exists
                                    if (circuits[CircuitNum].HasInsulation) {
                                        Beta = this->Cur.CurSimTimeStepSize /
                                               (cell.PipeCellData.Insulation.Properties.Density *
                                                cell.PipeCellData.Insulation.XY_CrossSectArea() *
                                                cell.depth() * cell.PipeCellData.Insulation.Properties.SpecificHeat);
                                        cell.PipeCellData.Insulation.Beta = Beta;
                                    }

                                    //'set the pipe cell
                                    Beta = this->Cur.CurSimTimeStepSize /
                                           (cell.PipeCellData.Pipe.Properties.Density *
                                            cell.PipeCellData.Pipe.XY_CrossSectArea() * cell.depth() *
                                            cell.PipeCellData.Pipe.Properties.SpecificHeat);
                                    cell.PipeCellData.Pipe.Beta = Beta;

                                    // now the fluid cell also
                                    cell.PipeCellData.Fluid.Properties = circuits[CircuitNum].CurFluidPropertySet;
                                    cell.PipeCellData.Fluid.Beta = this->Cur.CurSimTimeStepSize /
                                                                   (cell.PipeCellData.Fluid.Properties.Density *
                                                                    cell.PipeCellData.Fluid.Volume *
                                                                    cell.PipeCellData.Fluid.Properties.SpecificHeat);
                                }
                                break;
                            case CellType::BasementCutaway:
                                break;
                            case CellType::Unknown:
                                assert(false);
                        }
                    }
                }
            }
        }

        void Domain::DoEndOfIterationOperations(bool &Finished) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            // SUBROUTINE PARAMETER DEFINITIONS:
            static std::string const RoutineName("DoEndOfIterationOperations");

            //'check if we have converged for this iteration
            Finished = this->IsConverged_CurrentToPrevIteration();

            //'check for out of range temperatures here so they aren't plotted
            //'this routine should be *much* more restrictive than the exceptions, so we should be safe with this location
            bool OutOfRange = this->CheckForOutOfRangeTemps();
            if (OutOfRange) {
                if (this->HasZoneCoupledSlab) {
                    ShowSevereError("Site:GroundDomain:Slab" + RoutineName +
                                    ": Out of range temperatures detected in the ground domain.");
                    ShowContinueError("This could be due to the size of the loads on the domain.");
                    ShowContinueError("Verify inputs are correct. If problem persists, notify EnergyPlus support.");
                    ShowFatalError("Preceding error(s) cause program termination");
                } else if (this->HasZoneCoupledBasement) {
                    ShowSevereError("Site:GroundDomain:Basement" + RoutineName +
                                    ": Out of range temperatures detected in the ground domain.");
                    ShowContinueError("This could be due to the size of the loads on the domain.");
                    ShowContinueError("Verify inputs are correct. If problem persists, notify EnergyPlus support.");
                    ShowFatalError("Preceding error(s) cause program termination");
                } else {
                    ShowSevereError("PipingSystems:" + RoutineName +
                                    ": Out of range temperatures detected in piping system simulation.");
                    ShowContinueError(
                            "This could be due to the size of the pipe circuit in relation to the loads being imposed.");
                    ShowContinueError("Try increasing the size of the pipe circuit and investigate sizing effects.");
                    ShowFatalError("Preceding error(s) cause program termination");
                }
            }
        }

        void Domain::InitializeSoilMoistureCalcs() {

            // These vary by domain now, so we must be careful to retrieve them every time
            Real64 const Theta_liq = this->Moisture.Theta_liq;
            Real64 const Theta_sat = this->Moisture.Theta_sat;

            // Assumption
            Real64 const Theta_ice = Theta_liq;

            //'Cp (freezing) calculations
            Real64 const rho_ice = 917.0;  //'Kg / m3
            Real64 const rho_liq = 1000.0; //'kg / m3

            //'from( " An improved model for predicting soil thermal conductivity from water content at room temperature, Fig 4" )
            Real64 const CP_liq = 4180.0;    //'J / KgK
            Real64 const CP_ice = 2066.0;    //'J / KgK
            Real64 const Lat_fus = 334000.0; //'J / Kg
            Real64 const Cp_transient = Lat_fus / 0.4 + (0.5 * CP_ice - (CP_liq + CP_ice) / 2.0 * 0.1) / 0.4;

            //'from( " Numerical and experimental investigation of melting and freezing processes in phase change material storage" )
            this->Moisture.rhoCp_soil_liq_1 = 1225000.0 / (1.0 - Theta_sat); //'J/m3K
            this->Moisture.rhoCP_soil_liq =
                    this->Moisture.rhoCp_soil_liq_1 * (1.0 - Theta_sat) + rho_liq * CP_liq * Theta_liq;
            this->Moisture.rhoCP_soil_transient =
                    this->Moisture.rhoCp_soil_liq_1 * (1.0 - Theta_sat) +
                    ((rho_liq + rho_ice) / 2.0) * Cp_transient * Theta_ice;
            this->Moisture.rhoCP_soil_ice =
                    this->Moisture.rhoCp_soil_liq_1 * (1.0 - Theta_sat) + rho_ice * CP_ice * Theta_ice; //'!J / m3K
        }

        void Domain::EvaluateSoilRhoCp(Real64 const CellTemp, Real64 &rhoCp) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            //'set some temperatures here for generalization -- these could be set in the input file
            Real64 const frzAllIce = -0.5;
            Real64 const frzIceTrans = -0.4;
            Real64 const frzLiqTrans = -0.1;
            Real64 const frzAllLiq = 0.0;

            //'calculate this cell's new Cp value based on the cell temperature
            if (CellTemp <= frzAllIce) { // totally frozen
                rhoCp = this->Moisture.rhoCP_soil_ice;
            } else if ((CellTemp > frzAllIce) &&
                       (CellTemp < frzIceTrans)) { // in between totally frozen and ice transition
                rhoCp = this->Moisture.rhoCP_soil_ice +
                        (this->Moisture.rhoCP_soil_transient - this->Moisture.rhoCP_soil_ice) /
                        (frzIceTrans - frzAllIce) * (CellTemp - frzAllIce);
            } else if ((CellTemp >= frzIceTrans) &&
                       (CellTemp <= frzLiqTrans)) { // in between ice transition and liquid transition
                rhoCp = this->Moisture.rhoCP_soil_transient;
            } else if ((CellTemp > frzLiqTrans) &&
                       (CellTemp < frzAllLiq)) { // in between liquid transition and all liquid
                rhoCp = this->Moisture.rhoCp_soil_liq_1 +
                        (this->Moisture.rhoCP_soil_transient - this->Moisture.rhoCP_soil_liq) /
                        (frzAllLiq - frzLiqTrans) * (frzAllLiq - CellTemp);
            } else { // ( CellTemp >= frzAllLiq ) --- greater than or equal to all liquid
                rhoCp = this->Moisture.rhoCp_soil_liq_1;
            }
        }

        void CartesianCell::EvaluateNeighborCoordinates(Direction const CurDirection, int &NX, int &NY, int &NZ) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            int const X = this->X_index;
            int const Y = this->Y_index;
            int const Z = this->Z_index;

            switch (CurDirection) {
                case Direction::PositiveY:
                    NX = X;
                    NY = Y + 1;
                    NZ = Z;
                    break;
                case Direction::NegativeY:
                    NX = X;
                    NY = Y - 1;
                    NZ = Z;
                    break;
                case Direction::PositiveX:
                    NX = X + 1;
                    NY = Y;
                    NZ = Z;
                    break;
                case Direction::NegativeX:
                    NX = X - 1;
                    NY = Y;
                    NZ = Z;
                    break;
                case Direction::PositiveZ:
                    NX = X;
                    NY = Y;
                    NZ = Z + 1;
                    break;
                case Direction::NegativeZ:
                    NX = X;
                    NY = Y;
                    NZ = Z - 1;
                    break;
                default:
                    assert(false);
            }
        }

        void Domain::EvaluateNeighborCharacteristics(CartesianCell &ThisCell,
                                                     Direction const CurDirection,
                                                     Real64 &NeighborTemp,
                                                     Real64 &Resistance,
                                                     Real64 &AdiabaticMultiplier) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            int NX = 0, NY = 0, NZ = 0;
            ThisCell.EvaluateNeighborCoordinates(CurDirection, NX, NY, NZ);

            //'split effects between the two cells so we can carefully calculate resistance values
            Real64 ThisCellLength;
            Real64 NeighborCellLength;
            Real64 ThisCellConductivity = 10000.0;
            if (ThisCell.Properties.Conductivity > 0.0) ThisCellConductivity = ThisCell.Properties.Conductivity;
            Real64 NeighborConductivity = 10000.0;
            auto const &cell(this->Cells(NX, NY, NZ));
            if (cell.Properties.Conductivity > 0.0) NeighborConductivity = cell.Properties.Conductivity;

            //'calculate normal surface area
            Real64 const ThisNormalArea = ThisCell.normalArea(CurDirection);

            //'set distance based on cell types
            auto const &TempNeighborInfo = ThisCell.NeighborInfo[CurDirection];
            if (ThisCell.cellType == CellType::Pipe) {
                //'we need to be a bit careful with pipes, as they are full centroid to centroid in the z direction,
                //' but only centroid to wall in the x and y directions
                if (CurDirection == Direction::NegativeZ || CurDirection == Direction::PositiveZ) {
                    ThisCellLength = TempNeighborInfo.ThisCentroidToNeighborWall;
                    NeighborCellLength = TempNeighborInfo.ThisWallToNeighborCentroid;
                } else {
                    ThisCellLength = 0.0;
                    NeighborCellLength = TempNeighborInfo.ThisWallToNeighborCentroid;
                }
            } else if (cell.cellType == CellType::Pipe) {
                ThisCellLength = TempNeighborInfo.ThisCentroidToNeighborWall;
                NeighborCellLength = 0.0;
            } else {
                ThisCellLength = TempNeighborInfo.ThisCentroidToNeighborWall;
                NeighborCellLength = TempNeighborInfo.ThisWallToNeighborCentroid;
            }

            //'calculate resistance based on different conductivities between the two cells
            Resistance = (ThisCellLength / (ThisNormalArea * ThisCellConductivity)) +
                         (NeighborCellLength / (ThisNormalArea * NeighborConductivity));

            //'return proper temperature for the given simulation type
            NeighborTemp = cell.Temperature;

            //'return the adiabatic multiplier
            AdiabaticMultiplier = TempNeighborInfo.adiabaticMultiplier;
        }

        void
        Domain::EvaluateCellNeighborDirections(CartesianCell const &cell, int &NumFieldCells, int &NumBoundaryCells) {

            // SUBROUTINE INFORMATION:
            //       AUTHOR         Edwin Lee
            //       DATE WRITTEN   Summer 2011
            //       MODIFIED       na
            //       RE-ENGINEERED  na

            NumFieldCells = -1;
            NumBoundaryCells = -1;

            if (cell.X_index < this->x_max_index) {
                ++NumFieldCells;
                this->NeighborFieldCells[NumFieldCells] = Direction::PositiveX;
            } else {
                ++NumBoundaryCells;
                this->NeighborBoundaryCells[NumBoundaryCells] = Direction::PositiveX;
            }

            if (cell.X_index > 0) {
                ++NumFieldCells;
                this->NeighborFieldCells[NumFieldCells] = Direction::NegativeX;
            } else {
                ++NumBoundaryCells;
                this->NeighborBoundaryCells[NumBoundaryCells] = Direction::NegativeX;
            }

            if (cell.Y_index < this->y_max_index) {
                ++NumFieldCells;
                this->NeighborFieldCells[NumFieldCells] = Direction::PositiveY;
            } else {
                ++NumBoundaryCells;
                this->NeighborBoundaryCells[NumBoundaryCells] = Direction::PositiveY;
            }

            if (cell.Y_index > 0) {
                ++NumFieldCells;
                this->NeighborFieldCells[NumFieldCells] = Direction::NegativeY;
            } else {
                ++NumBoundaryCells;
                this->NeighborBoundaryCells[NumBoundaryCells] = Direction::NegativeY;
            }

            if (cell.Z_index < this->z_max_index) {
                ++NumFieldCells;
                this->NeighborFieldCells[NumFieldCells] = Direction::PositiveZ;
            } else {
                ++NumBoundaryCells;
                this->NeighborBoundaryCells[NumBoundaryCells] = Direction::PositiveZ;
            }

            if (cell.Z_index > 0) {
                ++NumFieldCells;
                this->NeighborFieldCells[NumFieldCells] = Direction::NegativeZ;
            } else {
                ++NumBoundaryCells;
                this->NeighborBoundaryCells[NumBoundaryCells] = Direction::NegativeZ;
            }
        }

    } // namespace PlantPipingSystemsManager

} // namespace EnergyPlus
