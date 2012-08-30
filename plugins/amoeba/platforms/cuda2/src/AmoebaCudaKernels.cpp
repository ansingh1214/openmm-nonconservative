/* -------------------------------------------------------------------------- *
 *                               OpenMMAmoeba                                 *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2008-2012 Stanford University and the Authors.      *
 * Authors: Peter Eastman, Mark Friedrichs                                    *
 * Contributors:                                                              *
 *                                                                            *
 * This program is free software: you can redistribute it and/or modify       *
 * it under the terms of the GNU Lesser General Public License as published   *
 * by the Free Software Foundation, either version 3 of the License, or       *
 * (at your option) any later version.                                        *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU Lesser General Public License for more details.                        *
 *                                                                            *
 * You should have received a copy of the GNU Lesser General Public License   *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.      *
 * -------------------------------------------------------------------------- */

#include "AmoebaCudaKernels.h"
#include "CudaAmoebaKernelSources.h"
#include "openmm/internal/ContextImpl.h"
#include "openmm/internal/AmoebaMultipoleForceImpl.h"
#include "openmm/internal/AmoebaWcaDispersionForceImpl.h"
#include "openmm/internal/AmoebaTorsionTorsionForceImpl.h"
#include "openmm/internal/AmoebaVdwForceImpl.h"
#include "openmm/internal/NonbondedForceImpl.h"
#include "CudaBondedUtilities.h"
#include "CudaForceInfo.h"
#include "CudaKernelSources.h"
#include "CudaNonbondedUtilities.h"

#include <cmath>
#ifdef _MSC_VER
#include <windows.h>
#endif

using namespace OpenMM;
using namespace std;

#define CHECK_RESULT(result) \
    if (result != CUDA_SUCCESS) { \
        std::stringstream m; \
        m<<errorMessage<<": "<<cu.getErrorString(result)<<" ("<<result<<")"<<" at "<<__FILE__<<":"<<__LINE__; \
        throw OpenMMException(m.str());\
    }

/* -------------------------------------------------------------------------- *
 *                           AmoebaHarmonicBond                               *
 * -------------------------------------------------------------------------- */

class CudaCalcAmoebaHarmonicBondForceKernel::ForceInfo : public CudaForceInfo {
public:
    ForceInfo(const AmoebaHarmonicBondForce& force) : force(force) {
    }
    int getNumParticleGroups() {
        return force.getNumBonds();
    }
    void getParticlesInGroup(int index, std::vector<int>& particles) {
        int particle1, particle2;
        double length, k;
        force.getBondParameters(index, particle1, particle2, length, k);
        particles.resize(2);
        particles[0] = particle1;
        particles[1] = particle2;
    }
    bool areGroupsIdentical(int group1, int group2) {
        int particle1, particle2;
        double length1, length2, k1, k2;
        force.getBondParameters(group1, particle1, particle2, length1, k1);
        force.getBondParameters(group2, particle1, particle2, length2, k2);
        return (length1 == length2 && k1 == k2);
    }
private:
    const AmoebaHarmonicBondForce& force;
};

CudaCalcAmoebaHarmonicBondForceKernel::CudaCalcAmoebaHarmonicBondForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) : 
                CalcAmoebaHarmonicBondForceKernel(name, platform), cu(cu), system(system), params(NULL) {
}

CudaCalcAmoebaHarmonicBondForceKernel::~CudaCalcAmoebaHarmonicBondForceKernel() {
    cu.setAsCurrent();
    if (params != NULL)
        delete params;
}

void CudaCalcAmoebaHarmonicBondForceKernel::initialize(const System& system, const AmoebaHarmonicBondForce& force) {
    cu.setAsCurrent();
    int numContexts = cu.getPlatformData().contexts.size();
    int startIndex = cu.getContextIndex()*force.getNumBonds()/numContexts;
    int endIndex = (cu.getContextIndex()+1)*force.getNumBonds()/numContexts;
    numBonds = endIndex-startIndex;
    if (numBonds == 0)
        return;
    vector<vector<int> > atoms(numBonds, vector<int>(2));
    params = CudaArray::create<float2>(cu, numBonds, "bondParams");
    vector<float2> paramVector(numBonds);
    for (int i = 0; i < numBonds; i++) {
        double length, k;
        force.getBondParameters(startIndex+i, atoms[i][0], atoms[i][1], length, k);
        paramVector[i] = make_float2((float) length, (float) k);
    }
    params->upload(paramVector);
    map<string, string> replacements;
    replacements["COMPUTE_FORCE"] = CudaAmoebaKernelSources::amoebaBondForce;
    replacements["PARAMS"] = cu.getBondedUtilities().addArgument(params->getDevicePointer(), "float2");
    replacements["CUBIC_K"] = cu.doubleToString(force.getAmoebaGlobalHarmonicBondCubic());
    replacements["QUARTIC_K"] = cu.doubleToString(force.getAmoebaGlobalHarmonicBondQuartic());
    cu.getBondedUtilities().addInteraction(atoms, cu.replaceStrings(CudaKernelSources::bondForce, replacements), force.getForceGroup());
    cu.addForce(new ForceInfo(force));
}

double CudaCalcAmoebaHarmonicBondForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    return 0.0;
}
//
///* -------------------------------------------------------------------------- *
// *                           AmoebaUreyBradley                               *
// * -------------------------------------------------------------------------- */
//
//class CudaCalcAmoebaUreyBradleyForceKernel::ForceInfo : public CudaForceInfo {
//public:
//    ForceInfo(const AmoebaUreyBradleyForce& force) : force(force) {
//    }
//    int getNumParticleGroups() {
//        return force.getNumInteractions();
//    }
//    void getParticlesInGroup(int index, std::vector<int>& particles) {
//        int particle1, particle2;
//        double length, k;
//        force.getUreyBradleyParameters(index, particle1, particle2, length, k);
//        particles.resize(2);
//        particles[0] = particle1;
//        particles[1] = particle2;
//    }
//    bool areGroupsIdentical(int group1, int group2) {
//        int particle1, particle2;
//        double length1, length2, k1, k2;
//        force.getUreyBradleyParameters(group1, particle1, particle2, length1, k1);
//        force.getUreyBradleyParameters(group2, particle1, particle2, length2, k2);
//        return (length1 == length2 && k1 == k2);
//    }
//private:
//    const AmoebaUreyBradleyForce& force;
//};
//
//CudaCalcAmoebaUreyBradleyForceKernel::CudaCalcAmoebaUreyBradleyForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) : 
//                CalcAmoebaUreyBradleyForceKernel(name, platform), cu(cu), system(system) {
//    data.incrementKernelCount();
//}
//
//CudaCalcAmoebaUreyBradleyForceKernel::~CudaCalcAmoebaUreyBradleyForceKernel() {
//    data.decrementKernelCount();
//}
//
//void CudaCalcAmoebaUreyBradleyForceKernel::initialize(const System& system, const AmoebaUreyBradleyForce& force) {
//
//    data.setAmoebaLocalForcesKernel( this );
//
//    numInteractions = force.getNumInteractions();
//    std::vector<int>   particle1(numInteractions);
//    std::vector<int>   particle2(numInteractions);
//    std::vector<float> length(numInteractions);
//    std::vector<float> quadratic(numInteractions);
//
//    for (int i = 0; i < numInteractions; i++) {
//
//        int particle1Index, particle2Index;
//        double lengthValue, kValue;
//        force.getUreyBradleyParameters(i, particle1Index, particle2Index, lengthValue, kValue );
//
//        particle1[i]     = particle1Index; 
//        particle2[i]     = particle2Index; 
//        length[i]        = static_cast<float>( lengthValue );
//        quadratic[i]     = static_cast<float>( kValue );
//    } 
//    gpuSetAmoebaUreyBradleyParameters( data.getAmoebaGpu(), particle1, particle2, length, quadratic, 
//                                       static_cast<float>(force.getAmoebaGlobalUreyBradleyCubic()),
//                                       static_cast<float>(force.getAmoebaGlobalUreyBradleyQuartic()) );
//    data.getAmoebaGpu()->gpuContext->forces.push_back(new ForceInfo(force));
//}
//
//double CudaCalcAmoebaUreyBradleyForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
//    if( data.getAmoebaLocalForcesKernel() == this ){
//        computeAmoebaLocalForces( data );
//    }
//    return 0.0;
//}

/* -------------------------------------------------------------------------- *
 *                           AmoebaHarmonicAngle                              *
 * -------------------------------------------------------------------------- */

class CudaCalcAmoebaHarmonicAngleForceKernel::ForceInfo : public CudaForceInfo {
public:
    ForceInfo(const AmoebaHarmonicAngleForce& force) : force(force) {
    }
    int getNumParticleGroups() {
        return force.getNumAngles();
    }
    void getParticlesInGroup(int index, std::vector<int>& particles) {
        int particle1, particle2, particle3;
        double angle, k;
        force.getAngleParameters(index, particle1, particle2, particle3, angle, k);
        particles.resize(3);
        particles[0] = particle1;
        particles[1] = particle2;
        particles[2] = particle3;
    }
    bool areGroupsIdentical(int group1, int group2) {
        int particle1, particle2, particle3;
        double angle1, angle2, k1, k2;
        force.getAngleParameters(group1, particle1, particle2, particle3, angle1, k1);
        force.getAngleParameters(group2, particle1, particle2, particle3, angle2, k2);
        return (angle1 == angle2 && k1 == k2);
    }
private:
    const AmoebaHarmonicAngleForce& force;
};

CudaCalcAmoebaHarmonicAngleForceKernel::CudaCalcAmoebaHarmonicAngleForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) :
            CalcAmoebaHarmonicAngleForceKernel(name, platform), cu(cu), system(system), params(NULL) {
}

CudaCalcAmoebaHarmonicAngleForceKernel::~CudaCalcAmoebaHarmonicAngleForceKernel() {
    cu.setAsCurrent();
    if (params != NULL)
        delete params;
}

void CudaCalcAmoebaHarmonicAngleForceKernel::initialize(const System& system, const AmoebaHarmonicAngleForce& force) {
    cu.setAsCurrent();
    int numContexts = cu.getPlatformData().contexts.size();
    int startIndex = cu.getContextIndex()*force.getNumAngles()/numContexts;
    int endIndex = (cu.getContextIndex()+1)*force.getNumAngles()/numContexts;
    numAngles = endIndex-startIndex;
    if (numAngles == 0)
        return;
    vector<vector<int> > atoms(numAngles, vector<int>(3));
    params = CudaArray::create<float2>(cu, numAngles, "angleParams");
    vector<float2> paramVector(numAngles);
    for (int i = 0; i < numAngles; i++) {
        double angle, k;
        force.getAngleParameters(startIndex+i, atoms[i][0], atoms[i][1], atoms[i][2], angle, k);
        paramVector[i] = make_float2((float) angle, (float) k);
    }
    params->upload(paramVector);
    map<string, string> replacements;
    replacements["COMPUTE_FORCE"] = CudaAmoebaKernelSources::amoebaAngleForce;
    replacements["PARAMS"] = cu.getBondedUtilities().addArgument(params->getDevicePointer(), "float2");
    replacements["CUBIC_K"] = cu.doubleToString(force.getAmoebaGlobalHarmonicAngleCubic());
    replacements["QUARTIC_K"] = cu.doubleToString(force.getAmoebaGlobalHarmonicAngleQuartic());
    replacements["PENTIC_K"] = cu.doubleToString(force.getAmoebaGlobalHarmonicAnglePentic());
    replacements["SEXTIC_K"] = cu.doubleToString(force.getAmoebaGlobalHarmonicAngleSextic());
    replacements["RAD_TO_DEG"] = cu.doubleToString(180/M_PI);
    cu.getBondedUtilities().addInteraction(atoms, cu.replaceStrings(CudaKernelSources::angleForce, replacements), force.getForceGroup());
    cu.addForce(new ForceInfo(force));
}

double CudaCalcAmoebaHarmonicAngleForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    return 0.0;
}

/* -------------------------------------------------------------------------- *
 *                           AmoebaHarmonicInPlaneAngle                       *
 * -------------------------------------------------------------------------- */

class CudaCalcAmoebaHarmonicInPlaneAngleForceKernel::ForceInfo : public CudaForceInfo {
public:
    ForceInfo(const AmoebaHarmonicInPlaneAngleForce& force) : force(force) {
    }
    int getNumParticleGroups() {
        return force.getNumAngles();
    }
    void getParticlesInGroup(int index, std::vector<int>& particles) {
        int particle1, particle2, particle3, particle4;
        double angle, k;
        force.getAngleParameters(index, particle1, particle2, particle3, particle4, angle, k);
        particles.resize(4);
        particles[0] = particle1;
        particles[1] = particle2;
        particles[2] = particle3;
        particles[3] = particle4;
    }
    bool areGroupsIdentical(int group1, int group2) {
        int particle1, particle2, particle3, particle4;
        double angle1, angle2, k1, k2;
        force.getAngleParameters(group1, particle1, particle2, particle3, particle4, angle1, k1);
        force.getAngleParameters(group2, particle1, particle2, particle3, particle4, angle2, k2);
        return (angle1 == angle2 && k1 == k2);
    }
private:
    const AmoebaHarmonicInPlaneAngleForce& force;
};

CudaCalcAmoebaHarmonicInPlaneAngleForceKernel::CudaCalcAmoebaHarmonicInPlaneAngleForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) : 
          CalcAmoebaHarmonicInPlaneAngleForceKernel(name, platform), cu(cu), system(system), params(NULL) {
}

CudaCalcAmoebaHarmonicInPlaneAngleForceKernel::~CudaCalcAmoebaHarmonicInPlaneAngleForceKernel() {
    cu.setAsCurrent();
    if (params != NULL)
        delete params;
}

void CudaCalcAmoebaHarmonicInPlaneAngleForceKernel::initialize(const System& system, const AmoebaHarmonicInPlaneAngleForce& force) {
    cu.setAsCurrent();
    int numContexts = cu.getPlatformData().contexts.size();
    int startIndex = cu.getContextIndex()*force.getNumAngles()/numContexts;
    int endIndex = (cu.getContextIndex()+1)*force.getNumAngles()/numContexts;
    numAngles = endIndex-startIndex;
    if (numAngles == 0)
        return;
    vector<vector<int> > atoms(numAngles, vector<int>(4));
    params = CudaArray::create<float2>(cu, numAngles, "angleParams");
    vector<float2> paramVector(numAngles);
    for (int i = 0; i < numAngles; i++) {
        double angle, k;
        force.getAngleParameters(startIndex+i, atoms[i][0], atoms[i][1], atoms[i][2], atoms[i][3], angle, k);
        paramVector[i] = make_float2((float) angle, (float) k);
    }
    params->upload(paramVector);
    map<string, string> replacements;
    replacements["PARAMS"] = cu.getBondedUtilities().addArgument(params->getDevicePointer(), "float2");
    replacements["CUBIC_K"] = cu.doubleToString(force.getAmoebaGlobalHarmonicInPlaneAngleCubic());
    replacements["QUARTIC_K"] = cu.doubleToString(force.getAmoebaGlobalHarmonicInPlaneAngleQuartic());
    replacements["PENTIC_K"] = cu.doubleToString(force.getAmoebaGlobalHarmonicInPlaneAnglePentic());
    replacements["SEXTIC_K"] = cu.doubleToString(force.getAmoebaGlobalHarmonicInPlaneAngleSextic());
    replacements["RAD_TO_DEG"] = cu.doubleToString(180/M_PI);
    cu.getBondedUtilities().addInteraction(atoms, cu.replaceStrings(CudaAmoebaKernelSources::amoebaInPlaneForce, replacements), force.getForceGroup());
    cu.addForce(new ForceInfo(force));
}

double CudaCalcAmoebaHarmonicInPlaneAngleForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    return 0.0;
}

///* -------------------------------------------------------------------------- *
// *                               AmoebaTorsion                                *
// * -------------------------------------------------------------------------- */
//
//class CudaCalcAmoebaTorsionForceKernel::ForceInfo : public CudaForceInfo {
//public:
//    ForceInfo(const AmoebaTorsionForce& force) : force(force) {
//    }
//    int getNumParticleGroups() {
//        return force.getNumTorsions();
//    }
//    void getParticlesInGroup(int index, std::vector<int>& particles) {
//        int particle1, particle2, particle3, particle4;
//        vector<double> torsion1, torsion2, torsion3;
//        force.getTorsionParameters(index, particle1, particle2, particle3, particle4, torsion1, torsion2, torsion3);
//        particles.resize(4);
//        particles[0] = particle1;
//        particles[1] = particle2;
//        particles[2] = particle3;
//        particles[3] = particle4;
//    }
//    bool areGroupsIdentical(int group1, int group2) {
//        int particle1, particle2, particle3, particle4;
//        vector<double> torsion11, torsion21, torsion31;
//        vector<double> torsion12, torsion22, torsion32;
//        force.getTorsionParameters(group1, particle1, particle2, particle3, particle4, torsion11, torsion21, torsion31);
//        force.getTorsionParameters(group2, particle1, particle2, particle3, particle4, torsion12, torsion22, torsion32);
//        for (int i = 0; i < (int) torsion11.size(); ++i)
//            if (torsion11[i] != torsion12[i])
//                return false;
//        for (int i = 0; i < (int) torsion21.size(); ++i)
//            if (torsion21[i] != torsion22[i])
//                return false;
//        for (int i = 0; i < (int) torsion31.size(); ++i)
//            if (torsion31[i] != torsion32[i])
//                return false;
//        return true;
//    }
//private:
//    const AmoebaTorsionForce& force;
//};
//
//CudaCalcAmoebaTorsionForceKernel::CudaCalcAmoebaTorsionForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) :
//             CalcAmoebaTorsionForceKernel(name, platform), cu(cu), system(system) {
//    data.incrementKernelCount();
//}
//
//CudaCalcAmoebaTorsionForceKernel::~CudaCalcAmoebaTorsionForceKernel() {
//    data.decrementKernelCount();
//}
//
//void CudaCalcAmoebaTorsionForceKernel::initialize(const System& system, const AmoebaTorsionForce& force) {
//
//    data.setAmoebaLocalForcesKernel( this );
//    numTorsions                     = force.getNumTorsions();
//    std::vector<int> particle1(numTorsions);
//    std::vector<int> particle2(numTorsions);
//    std::vector<int> particle3(numTorsions);
//    std::vector<int> particle4(numTorsions);
//
//    std::vector< std::vector<float> > torsionParameters1(numTorsions);
//    std::vector< std::vector<float> > torsionParameters2(numTorsions);
//    std::vector< std::vector<float> > torsionParameters3(numTorsions);
//
//    for (int i = 0; i < numTorsions; i++) {
//
//        std::vector<double> torsionParameter1;
//        std::vector<double> torsionParameter2;
//        std::vector<double> torsionParameter3;
//
//        std::vector<float> torsionParameters1F(3);
//        std::vector<float> torsionParameters2F(3);
//        std::vector<float> torsionParameters3F(3);
//
//        force.getTorsionParameters(i, particle1[i], particle2[i], particle3[i], particle4[i], torsionParameter1, torsionParameter2, torsionParameter3 );
//        for ( unsigned int jj = 0; jj < torsionParameter1.size(); jj++) {
//            torsionParameters1F[jj] = static_cast<float>(torsionParameter1[jj]);
//            torsionParameters2F[jj] = static_cast<float>(torsionParameter2[jj]);
//            torsionParameters3F[jj] = static_cast<float>(torsionParameter3[jj]);
//        }
//        torsionParameters1[i] = torsionParameters1F;
//        torsionParameters2[i] = torsionParameters2F;
//        torsionParameters3[i] = torsionParameters3F;
//    }
//    gpuSetAmoebaTorsionParameters(data.getAmoebaGpu(), particle1, particle2, particle3, particle4, torsionParameters1, torsionParameters2, torsionParameters3 );
//    data.getAmoebaGpu()->gpuContext->forces.push_back(new ForceInfo(force));
//}
//
//double CudaCalcAmoebaTorsionForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
//    if( data.getAmoebaLocalForcesKernel() == this ){
//        computeAmoebaLocalForces( data );
//    }
//    return 0.0;
//}

/* -------------------------------------------------------------------------- *
  *                              AmoebaPiTorsion                              *
 * -------------------------------------------------------------------------- */

class CudaCalcAmoebaPiTorsionForceKernel::ForceInfo : public CudaForceInfo {
public:
    ForceInfo(const AmoebaPiTorsionForce& force) : force(force) {
    }
    int getNumParticleGroups() {
        return force.getNumPiTorsions();
    }
    void getParticlesInGroup(int index, std::vector<int>& particles) {
        int particle1, particle2, particle3, particle4, particle5, particle6;
        double k;
        force.getPiTorsionParameters(index, particle1, particle2, particle3, particle4, particle5, particle6, k);
        particles.resize(6);
        particles[0] = particle1;
        particles[1] = particle2;
        particles[2] = particle3;
        particles[3] = particle4;
        particles[4] = particle5;
        particles[5] = particle6;
    }
    bool areGroupsIdentical(int group1, int group2) {
        int particle1, particle2, particle3, particle4, particle5, particle6;
        double k1, k2;
        force.getPiTorsionParameters(group1, particle1, particle2, particle3, particle4, particle5, particle6, k1);
        force.getPiTorsionParameters(group2, particle1, particle2, particle3, particle4, particle5, particle6, k2);
        return (k1 == k2);
    }
private:
    const AmoebaPiTorsionForce& force;
};

CudaCalcAmoebaPiTorsionForceKernel::CudaCalcAmoebaPiTorsionForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) :
         CalcAmoebaPiTorsionForceKernel(name, platform), cu(cu), system(system), params(NULL) {
}

CudaCalcAmoebaPiTorsionForceKernel::~CudaCalcAmoebaPiTorsionForceKernel() {
    cu.setAsCurrent();
    if (params != NULL)
        delete params;
}

void CudaCalcAmoebaPiTorsionForceKernel::initialize(const System& system, const AmoebaPiTorsionForce& force) {
    cu.setAsCurrent();
    int numContexts = cu.getPlatformData().contexts.size();
    int startIndex = cu.getContextIndex()*force.getNumPiTorsions()/numContexts;
    int endIndex = (cu.getContextIndex()+1)*force.getNumPiTorsions()/numContexts;
    numPiTorsions = endIndex-startIndex;
    if (numPiTorsions == 0)
        return;
    vector<vector<int> > atoms(numPiTorsions, vector<int>(6));
    params = CudaArray::create<float>(cu, numPiTorsions, "piTorsionParams");
    vector<float> paramVector(numPiTorsions);
    for (int i = 0; i < numPiTorsions; i++) {
        double k;
        force.getPiTorsionParameters(startIndex+i, atoms[i][0], atoms[i][1], atoms[i][2], atoms[i][3], atoms[i][4], atoms[i][5], k);
        paramVector[i] = (float) k;
    }
    params->upload(paramVector);
    map<string, string> replacements;
    replacements["PARAMS"] = cu.getBondedUtilities().addArgument(params->getDevicePointer(), "float");
    cu.getBondedUtilities().addInteraction(atoms, cu.replaceStrings(CudaAmoebaKernelSources::amoebaPiTorsionForce, replacements), force.getForceGroup());
    cu.addForce(new ForceInfo(force));
}

double CudaCalcAmoebaPiTorsionForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    return 0.0;
}

/* -------------------------------------------------------------------------- *
 *                           AmoebaStretchBend                                *
 * -------------------------------------------------------------------------- */

class CudaCalcAmoebaStretchBendForceKernel::ForceInfo : public CudaForceInfo {
public:
    ForceInfo(const AmoebaStretchBendForce& force) : force(force) {
    }
    int getNumParticleGroups() {
        return force.getNumStretchBends();
    }
    void getParticlesInGroup(int index, std::vector<int>& particles) {
        int particle1, particle2, particle3;
        double lengthAB, lengthCB, angle, k;
        force.getStretchBendParameters(index, particle1, particle2, particle3, lengthAB, lengthCB, angle, k);
        particles.resize(3);
        particles[0] = particle1;
        particles[1] = particle2;
        particles[2] = particle3;
    }
    bool areGroupsIdentical(int group1, int group2) {
        int particle1, particle2, particle3;
        double lengthAB1, lengthAB2, lengthCB1, lengthCB2, angle1, angle2, k1, k2;
        force.getStretchBendParameters(group1, particle1, particle2, particle3, lengthAB1, lengthCB1, angle1, k1);
        force.getStretchBendParameters(group2, particle1, particle2, particle3, lengthAB2, lengthCB2, angle2, k2);
        return (lengthAB1 == lengthAB2 && lengthCB1 == lengthCB2 && angle1 == angle2 && k1 == k2);
    }
private:
    const AmoebaStretchBendForce& force;
};

CudaCalcAmoebaStretchBendForceKernel::CudaCalcAmoebaStretchBendForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) :
                   CalcAmoebaStretchBendForceKernel(name, platform), cu(cu), system(system), params(NULL) {
}

CudaCalcAmoebaStretchBendForceKernel::~CudaCalcAmoebaStretchBendForceKernel() {
    cu.setAsCurrent();
    if (params != NULL)
        delete params;
}

void CudaCalcAmoebaStretchBendForceKernel::initialize(const System& system, const AmoebaStretchBendForce& force) {
    cu.setAsCurrent();
    int numContexts = cu.getPlatformData().contexts.size();
    int startIndex = cu.getContextIndex()*force.getNumStretchBends()/numContexts;
    int endIndex = (cu.getContextIndex()+1)*force.getNumStretchBends()/numContexts;
    numStretchBends = endIndex-startIndex;
    if (numStretchBends == 0)
        return;
    vector<vector<int> > atoms(numStretchBends, vector<int>(3));
    params = CudaArray::create<float4>(cu, numStretchBends, "stretchBendParams");
    vector<float4> paramVector(numStretchBends);
    for (int i = 0; i < numStretchBends; i++) {
        double lengthAB, lengthCB, angle, k;
        force.getStretchBendParameters(startIndex+i, atoms[i][0], atoms[i][1], atoms[i][2], lengthAB, lengthCB, angle, k);
        paramVector[i] = make_float4((float) lengthAB, (float) lengthCB, (float) angle, (float) k);
    }
    params->upload(paramVector);
    map<string, string> replacements;
    replacements["PARAMS"] = cu.getBondedUtilities().addArgument(params->getDevicePointer(), "float4");
    replacements["RAD_TO_DEG"] = cu.doubleToString(180/M_PI);
    cu.getBondedUtilities().addInteraction(atoms, cu.replaceStrings(CudaAmoebaKernelSources::amoebaStretchBendForce, replacements), force.getForceGroup());
    cu.addForce(new ForceInfo(force));
}

double CudaCalcAmoebaStretchBendForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
}

/* -------------------------------------------------------------------------- *
 *                           AmoebaOutOfPlaneBend                             *
 * -------------------------------------------------------------------------- */

class CudaCalcAmoebaOutOfPlaneBendForceKernel::ForceInfo : public CudaForceInfo {
public:
    ForceInfo(const AmoebaOutOfPlaneBendForce& force) : force(force) {
    }
    int getNumParticleGroups() {
        return force.getNumOutOfPlaneBends();
    }
    void getParticlesInGroup(int index, std::vector<int>& particles) {
        int particle1, particle2, particle3, particle4;
        double k;
        force.getOutOfPlaneBendParameters(index, particle1, particle2, particle3, particle4, k);
        particles.resize(4);
        particles[0] = particle1;
        particles[1] = particle2;
        particles[2] = particle3;
        particles[3] = particle4;
    }
    bool areGroupsIdentical(int group1, int group2) {
        int particle1, particle2, particle3, particle4;
        double k1, k2;
        force.getOutOfPlaneBendParameters(group1, particle1, particle2, particle3, particle4, k1);
        force.getOutOfPlaneBendParameters(group2, particle1, particle2, particle3, particle4, k2);
        return (k1 == k2);
    }
private:
    const AmoebaOutOfPlaneBendForce& force;
};

CudaCalcAmoebaOutOfPlaneBendForceKernel::CudaCalcAmoebaOutOfPlaneBendForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) :
          CalcAmoebaOutOfPlaneBendForceKernel(name, platform), cu(cu), system(system), params(NULL) {
}

CudaCalcAmoebaOutOfPlaneBendForceKernel::~CudaCalcAmoebaOutOfPlaneBendForceKernel() {
    cu.setAsCurrent();
    if (params != NULL)
        delete params;
}

void CudaCalcAmoebaOutOfPlaneBendForceKernel::initialize(const System& system, const AmoebaOutOfPlaneBendForce& force) {
    cu.setAsCurrent();
    int numContexts = cu.getPlatformData().contexts.size();
    int startIndex = cu.getContextIndex()*force.getNumOutOfPlaneBends()/numContexts;
    int endIndex = (cu.getContextIndex()+1)*force.getNumOutOfPlaneBends()/numContexts;
    numOutOfPlaneBends = endIndex-startIndex;
    if (numOutOfPlaneBends == 0)
        return;
    vector<vector<int> > atoms(numOutOfPlaneBends, vector<int>(4));
    params = CudaArray::create<float>(cu, numOutOfPlaneBends, "outOfPlaneParams");
    vector<float> paramVector(numOutOfPlaneBends);
    for (int i = 0; i < numOutOfPlaneBends; i++) {
        double k;
        force.getOutOfPlaneBendParameters(startIndex+i, atoms[i][0], atoms[i][1], atoms[i][2], atoms[i][3], k);
        paramVector[i] = (float) k;
    }
    params->upload(paramVector);
    map<string, string> replacements;
    replacements["PARAMS"] = cu.getBondedUtilities().addArgument(params->getDevicePointer(), "float");
    replacements["CUBIC_K"] = cu.doubleToString(force.getAmoebaGlobalOutOfPlaneBendCubic());
    replacements["QUARTIC_K"] = cu.doubleToString(force.getAmoebaGlobalOutOfPlaneBendQuartic());
    replacements["PENTIC_K"] = cu.doubleToString(force.getAmoebaGlobalOutOfPlaneBendPentic());
    replacements["SEXTIC_K"] = cu.doubleToString(force.getAmoebaGlobalOutOfPlaneBendSextic());
    replacements["RAD_TO_DEG"] = cu.doubleToString(180/M_PI);
    cu.getBondedUtilities().addInteraction(atoms, cu.replaceStrings(CudaAmoebaKernelSources::amoebaOutOfPlaneBendForce, replacements), force.getForceGroup());
    cu.addForce(new ForceInfo(force));
}

double CudaCalcAmoebaOutOfPlaneBendForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    return 0.0;
}

/* -------------------------------------------------------------------------- *
 *                           AmoebaTorsionTorsion                             *
 * -------------------------------------------------------------------------- */

class CudaCalcAmoebaTorsionTorsionForceKernel::ForceInfo : public CudaForceInfo {
public:
    ForceInfo(const AmoebaTorsionTorsionForce& force) : force(force) {
    }
    int getNumParticleGroups() {
        return force.getNumTorsionTorsions();
    }
    void getParticlesInGroup(int index, std::vector<int>& particles) {
        int particle1, particle2, particle3, particle4, particle5, chiralCheckAtomIndex, gridIndex;
        force.getTorsionTorsionParameters(index, particle1, particle2, particle3, particle4, particle5, chiralCheckAtomIndex, gridIndex);
        particles.resize(5);
        particles[0] = particle1;
        particles[1] = particle2;
        particles[2] = particle3;
        particles[3] = particle4;
        particles[4] = particle5;
    }
    bool areGroupsIdentical(int group1, int group2) {
        int particle1, particle2, particle3, particle4, particle5;
        int chiral1, chiral2, grid1, grid2;
        force.getTorsionTorsionParameters(group1, particle1, particle2, particle3, particle4, particle5, chiral1, grid1);
        force.getTorsionTorsionParameters(group2, particle1, particle2, particle3, particle4, particle5, chiral2, grid2);
        return (grid1 == grid2);
    }
private:
    const AmoebaTorsionTorsionForce& force;
};

CudaCalcAmoebaTorsionTorsionForceKernel::CudaCalcAmoebaTorsionTorsionForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) :
                CalcAmoebaTorsionTorsionForceKernel(name, platform), cu(cu), system(system), gridValues(NULL), gridParams(NULL), torsionParams(NULL) {
}

CudaCalcAmoebaTorsionTorsionForceKernel::~CudaCalcAmoebaTorsionTorsionForceKernel() {
    cu.setAsCurrent();
    if (gridValues != NULL)
        delete gridValues;
    if (gridParams != NULL)
        delete gridParams;
    if (torsionParams != NULL)
        delete torsionParams;
}

void CudaCalcAmoebaTorsionTorsionForceKernel::initialize(const System& system, const AmoebaTorsionTorsionForce& force) {
    cu.setAsCurrent();
    int numContexts = cu.getPlatformData().contexts.size();
    int startIndex = cu.getContextIndex()*force.getNumTorsionTorsions()/numContexts;
    int endIndex = (cu.getContextIndex()+1)*force.getNumTorsionTorsions()/numContexts;
    numTorsionTorsions = endIndex-startIndex;
    if (numTorsionTorsions == 0)
        return;
    
    // Record torsion parameters.
    
    vector<vector<int> > atoms(numTorsionTorsions, vector<int>(5));
    vector<int2> torsionParamsVec(numTorsionTorsions);
    torsionParams = CudaArray::create<int2>(cu, numTorsionTorsions, "torsionTorsionParams");
    for (int i = 0; i < numTorsionTorsions; i++)
        force.getTorsionTorsionParameters(startIndex+i, atoms[i][0], atoms[i][1], atoms[i][2], atoms[i][3], atoms[i][4], torsionParamsVec[i].x, torsionParamsVec[i].y);
    torsionParams->upload(torsionParamsVec);
    
    // Record the grids.
    
    vector<float4> gridValuesVec;
    vector<float4> gridParamsVec;
    for (int i = 0; i < force.getNumTorsionTorsionGrids(); i++) {
        const TorsionTorsionGrid& initialGrid = force.getTorsionTorsionGrid(i);

        // check if grid needs to be reordered: x-angle should be 'slow' index

        bool reordered = false;
        TorsionTorsionGrid reorderedGrid;
        if (initialGrid[0][0][0] != initialGrid[0][1][0]) {
            AmoebaTorsionTorsionForceImpl::reorderGrid(initialGrid, reorderedGrid);
            reordered = true;
        }
        const TorsionTorsionGrid& grid = (reordered ? reorderedGrid : initialGrid);
        float range = grid[0][grid[0].size()-1][1] - grid[0][0][1];
        gridParamsVec.push_back(make_float4(gridValuesVec.size(), grid[0][0][0], range/(grid.size()-1), grid.size()));
        for (int j = 0; j < grid.size(); j++)
            for (int k = 0; k < grid[j].size(); k++)
                gridValuesVec.push_back(make_float4((float) grid[j][k][2], (float) grid[j][k][3], (float) grid[j][k][4], (float) grid[j][k][5]));
    }
    gridValues = CudaArray::create<float4>(cu, gridValuesVec.size(), "torsionTorsionGridValues");
    gridParams = CudaArray::create<float4>(cu, gridParamsVec.size(), "torsionTorsionGridParams");
    gridValues->upload(gridValuesVec);
    gridParams->upload(gridParamsVec);
    map<string, string> replacements;
    replacements["GRID_VALUES"] = cu.getBondedUtilities().addArgument(gridValues->getDevicePointer(), "float4");
    replacements["GRID_PARAMS"] = cu.getBondedUtilities().addArgument(gridParams->getDevicePointer(), "float4");
    replacements["TORSION_PARAMS"] = cu.getBondedUtilities().addArgument(torsionParams->getDevicePointer(), "int2");
    replacements["RAD_TO_DEG"] = cu.doubleToString(180/M_PI);
    cu.getBondedUtilities().addInteraction(atoms, cu.replaceStrings(CudaAmoebaKernelSources::amoebaTorsionTorsionForce, replacements), force.getForceGroup());
    cu.getBondedUtilities().addPrefixCode(CudaAmoebaKernelSources::bicubic);
    cu.addForce(new ForceInfo(force));
}

double CudaCalcAmoebaTorsionTorsionForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    return 0.0;
}

/* -------------------------------------------------------------------------- *
 *                             AmoebaMultipole                                *
 * -------------------------------------------------------------------------- */

class CudaCalcAmoebaMultipoleForceKernel::ForceInfo : public CudaForceInfo {
public:
    ForceInfo(const AmoebaMultipoleForce& force) : force(force) {
    }
    bool areParticlesIdentical(int particle1, int particle2) {
        double charge1, charge2, thole1, thole2, damping1, damping2, polarity1, polarity2;
        int axis1, axis2, multipole11, multipole12, multipole21, multipole22, multipole31, multipole32;
        vector<double> dipole1, dipole2, quadrupole1, quadrupole2;
        force.getMultipoleParameters(particle1, charge1, dipole1, quadrupole1, axis1, multipole11, multipole21, multipole31, thole1, damping1, polarity1);
        force.getMultipoleParameters(particle2, charge2, dipole2, quadrupole2, axis2, multipole12, multipole22, multipole32, thole2, damping2, polarity2);
        if (charge1 != charge2 || thole1 != thole2 || damping1 != damping2 || polarity1 != polarity2 || axis1 != axis2){
            return false;
        }
        for (int i = 0; i < (int) dipole1.size(); ++i){
            if (dipole1[i] != dipole2[i]){
                return false;
            }
        }
        for (int i = 0; i < (int) quadrupole1.size(); ++i){
            if (quadrupole1[i] != quadrupole2[i]){
                return false;
            }
        }
        return true;
    }
private:
    const AmoebaMultipoleForce& force;
};

CudaCalcAmoebaMultipoleForceKernel::CudaCalcAmoebaMultipoleForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) : 
        CalcAmoebaMultipoleForceKernel(name, platform), cu(cu), system(system), hasInitializedScaleFactors(false), hasInitializedFFT(false),
        multipoleParticles(NULL), molecularDipoles(NULL), molecularQuadrupoles(NULL), labFrameDipoles(NULL), labFrameQuadrupoles(NULL),
        field(NULL), fieldPolar(NULL), inducedField(NULL), inducedFieldPolar(NULL), torque(NULL), dampingAndThole(NULL),
        inducedDipole(NULL), inducedDipolePolar(NULL), inducedDipoleErrors(NULL), polarizability(NULL), covalentFlags(NULL), polarizationGroupFlags(NULL),
        pmeGrid(NULL), pmeBsplineModuliX(NULL), pmeBsplineModuliY(NULL), pmeBsplineModuliZ(NULL), pmeTheta1(NULL), pmeTheta2(NULL), pmeTheta3(NULL),
        pmeIgrid(NULL), pmePhi(NULL), pmePhid(NULL), pmePhip(NULL), pmePhidp(NULL), pmeAtomRange(NULL), pmeAtomGridIndex(NULL), sort(NULL) {
}

CudaCalcAmoebaMultipoleForceKernel::~CudaCalcAmoebaMultipoleForceKernel() {
    cu.setAsCurrent();
    if (multipoleParticles != NULL)
        delete multipoleParticles;
    if (molecularDipoles != NULL)
        delete molecularDipoles;
    if (molecularQuadrupoles != NULL)
        delete molecularQuadrupoles;
    if (labFrameDipoles != NULL)
        delete labFrameDipoles;
    if (labFrameQuadrupoles != NULL)
        delete labFrameQuadrupoles;
    if (field != NULL)
        delete field;
    if (fieldPolar != NULL)
        delete fieldPolar;
    if (inducedField != NULL)
        delete inducedField;
    if (inducedFieldPolar != NULL)
        delete inducedFieldPolar;
    if (torque != NULL)
        delete torque;
    if (dampingAndThole != NULL)
        delete dampingAndThole;
    if (inducedDipole != NULL)
        delete inducedDipole;
    if (inducedDipolePolar != NULL)
        delete inducedDipolePolar;
    if (inducedDipoleErrors != NULL)
        delete inducedDipoleErrors;
    if (polarizability != NULL)
        delete polarizability;
    if (covalentFlags != NULL)
        delete covalentFlags;
    if (polarizationGroupFlags != NULL)
        delete polarizationGroupFlags;
    if (pmeGrid != NULL)
        delete pmeGrid;
    if (pmeBsplineModuliX != NULL)
        delete pmeBsplineModuliX;
    if (pmeBsplineModuliY != NULL)
        delete pmeBsplineModuliY;
    if (pmeBsplineModuliZ != NULL)
        delete pmeBsplineModuliZ;
    if (pmeTheta1 != NULL)
        delete pmeTheta1;
    if (pmeTheta2 != NULL)
        delete pmeTheta2;
    if (pmeTheta3 != NULL)
        delete pmeTheta3;
    if (pmeIgrid != NULL)
        delete pmeIgrid;
    if (pmePhi != NULL)
        delete pmePhi;
    if (pmePhid != NULL)
        delete pmePhid;
    if (pmePhip != NULL)
        delete pmePhip;
    if (pmePhidp != NULL)
        delete pmePhidp;
    if (pmeAtomRange != NULL)
        delete pmeAtomRange;
    if (pmeAtomGridIndex != NULL)
        delete pmeAtomGridIndex;
    if (sort != NULL)
        delete sort;
    if (hasInitializedFFT)
        cufftDestroy(fft);
}

/**
 * Select a size for an FFT that is a multiple of 2, 3, 5, and 7.
 */
static int findFFTDimension(int minimum) {
    if (minimum < 1)
        return 1;
    while (true) {
        // Attempt to factor the current value.

        int unfactored = minimum;
        for (int factor = 2; factor < 8; factor++) {
            while (unfactored > 1 && unfactored%factor == 0)
                unfactored /= factor;
        }
        if (unfactored == 1)
            return minimum;
        minimum++;
    }
}

void CudaCalcAmoebaMultipoleForceKernel::initialize(const System& system, const AmoebaMultipoleForce& force) {
    cu.setAsCurrent();

    // Initialize multipole parameters.

    numMultipoles = force.getNumMultipoles();
    CudaArray& posq = cu.getPosq();
    float4* posqf = (float4*) cu.getPinnedBuffer();
    double4* posqd = (double4*) cu.getPinnedBuffer();
    vector<float2> dampingAndTholeVec;
    vector<float> polarizabilityVec;
    vector<float> molecularDipolesVec;
    vector<float> molecularQuadrupolesVec;
    vector<int4> multipoleParticlesVec;
    for (int i = 0; i < numMultipoles; i++) {
        double charge, thole, damping, polarity;
        int axisType, atomX, atomY, atomZ;
        vector<double> dipole, quadrupole;
        force.getMultipoleParameters(i, charge, dipole, quadrupole, axisType, atomZ, atomX, atomY, thole, damping, polarity);
        if (cu.getUseDoublePrecision())
            posqd[i] = make_double4(0, 0, 0, charge);
        else
            posqf[i] = make_float4(0, 0, 0, (float) charge);
        dampingAndTholeVec.push_back(make_float2((float) damping, (float) thole));
        polarizabilityVec.push_back((float) polarity);
        multipoleParticlesVec.push_back(make_int4(atomX, atomY, atomZ, axisType));
        for (int j = 0; j < 3; j++)
            molecularDipolesVec.push_back((float) dipole[j]);
        molecularQuadrupolesVec.push_back((float) quadrupole[0]);
        molecularQuadrupolesVec.push_back((float) quadrupole[1]);
        molecularQuadrupolesVec.push_back((float) quadrupole[2]);
        molecularQuadrupolesVec.push_back((float) quadrupole[4]);
        molecularQuadrupolesVec.push_back((float) quadrupole[5]);
    }
    int paddedNumAtoms = cu.getPaddedNumAtoms();
    for (int i = numMultipoles; i < paddedNumAtoms; i++) {
        dampingAndTholeVec.push_back(make_float2(0, 0));
        polarizabilityVec.push_back(0);
        multipoleParticlesVec.push_back(make_int4(0, 0, 0, 0));
        for (int j = 0; j < 3; j++)
            molecularDipolesVec.push_back(0);
        for (int j = 0; j < 5; j++)
            molecularQuadrupolesVec.push_back(0);
    }
    dampingAndThole = CudaArray::create<float2>(cu, paddedNumAtoms, "dampingAndThole");
    polarizability = CudaArray::create<float>(cu, paddedNumAtoms, "polarizability");
    multipoleParticles = CudaArray::create<int4>(cu, paddedNumAtoms, "multipoleParticles");
    molecularDipoles = CudaArray::create<float>(cu, 3*paddedNumAtoms, "molecularDipoles");
    molecularQuadrupoles = CudaArray::create<float>(cu, 5*paddedNumAtoms, "molecularQuadrupoles");
    dampingAndThole->upload(dampingAndTholeVec);
    polarizability->upload(polarizabilityVec);
    multipoleParticles->upload(multipoleParticlesVec);
    molecularDipoles->upload(molecularDipolesVec);
    molecularQuadrupoles->upload(molecularQuadrupolesVec);
    posq.upload(cu.getPinnedBuffer());
    
    // Create workspace arrays.
    
    int elementSize = (cu.getUseDoublePrecision() ? sizeof(double) : sizeof(float));
    labFrameDipoles = new CudaArray(cu, 3*paddedNumAtoms, elementSize, "labFrameDipoles");
    labFrameQuadrupoles = new CudaArray(cu, 9*paddedNumAtoms, elementSize, "labFrameQuadrupoles");
    field = new CudaArray(cu, 3*paddedNumAtoms, sizeof(long long), "field");
    fieldPolar = new CudaArray(cu, 3*paddedNumAtoms, sizeof(long long), "fieldPolar");
    torque = new CudaArray(cu, 3*paddedNumAtoms, sizeof(long long), "torque");
    inducedDipole = new CudaArray(cu, 3*paddedNumAtoms, elementSize, "inducedDipole");
    inducedDipolePolar = new CudaArray(cu, 3*paddedNumAtoms, elementSize, "inducedDipolePolar");
    inducedDipoleErrors = new CudaArray(cu, cu.getNumThreadBlocks(), sizeof(float2), "inducedDipoleErrors");
    cu.addAutoclearBuffer(*field);
    cu.addAutoclearBuffer(*fieldPolar);
    cu.addAutoclearBuffer(*torque);
    
    // Record which atoms should be flagged as exclusions based on covalent groups, and determine
    // the values for the covalent group flags.
    
    vector<vector<int> > exclusions(numMultipoles);
    for (int i = 0; i < numMultipoles; i++) {
        vector<int> atoms;
        set<int> allAtoms;
        allAtoms.insert(i);
        force.getCovalentMap(i, AmoebaMultipoleForce::Covalent12, atoms);
        allAtoms.insert(atoms.begin(), atoms.end());
        force.getCovalentMap(i, AmoebaMultipoleForce::Covalent13, atoms);
        allAtoms.insert(atoms.begin(), atoms.end());
        for (set<int>::const_iterator iter = allAtoms.begin(); iter != allAtoms.end(); ++iter)
            covalentFlagValues.push_back(make_int3(i, *iter, 0));
        force.getCovalentMap(i, AmoebaMultipoleForce::Covalent14, atoms);
        allAtoms.insert(atoms.begin(), atoms.end());
        for (int j = 0; j < (int) atoms.size(); j++)
            covalentFlagValues.push_back(make_int3(i, atoms[j], 1));
        force.getCovalentMap(i, AmoebaMultipoleForce::Covalent15, atoms);
        for (int j = 0; j < (int) atoms.size(); j++)
            covalentFlagValues.push_back(make_int3(i, atoms[j], 2));
        allAtoms.insert(atoms.begin(), atoms.end());
        force.getCovalentMap(i, AmoebaMultipoleForce::PolarizationCovalent11, atoms);
        allAtoms.insert(atoms.begin(), atoms.end());
        exclusions[i].insert(exclusions[i].end(), allAtoms.begin(), allAtoms.end());
        for (int j = 0; j < (int) atoms.size(); j++)
            polarizationFlagValues.push_back(make_int2(i, atoms[j]));
    }
    
    // Record other options.
    
    if (force.getPolarizationType() == AmoebaMultipoleForce::Mutual) {
        maxInducedIterations = force.getMutualInducedMaxIterations();
        inducedEpsilon = force.getMutualInducedTargetEpsilon();
        inducedField = new CudaArray(cu, 3*paddedNumAtoms, sizeof(long long), "inducedField");
        inducedFieldPolar = new CudaArray(cu, 3*paddedNumAtoms, sizeof(long long), "inducedFieldPolar");
    }
    else
        maxInducedIterations = 0;
    bool usePME = (force.getNonbondedMethod() == AmoebaMultipoleForce::PME);
    
    // Create the kernels.

    map<string, string> defines;
    defines["NUM_ATOMS"] = cu.intToString(numMultipoles);
    defines["PADDED_NUM_ATOMS"] = cu.intToString(cu.getPaddedNumAtoms());
    defines["THREAD_BLOCK_SIZE"] = cu.intToString(cu.getNonbondedUtilities().getForceThreadBlockSize());
    defines["NUM_BLOCKS"] = cu.intToString(cu.getNumAtomBlocks());
    defines["ENERGY_SCALE_FACTOR"] = cu.doubleToString(138.9354558456); // DIVIDE BY INNER DIELECTRIC!!!
    if (force.getPolarizationType() == AmoebaMultipoleForce::Direct)
        defines["DIRECT_POLARIZATION"] = "";
    double alpha = force.getAEwald();
    int gridSizeX, gridSizeY, gridSizeZ;
    if (usePME) {
        vector<int> pmeGridDimension;
        force.getPmeGridDimensions(pmeGridDimension);
        if (pmeGridDimension[0] == 0 || alpha == 0.0) {
            NonbondedForce nb;
            nb.setEwaldErrorTolerance(force.getEwaldErrorTolerance());
            nb.setCutoffDistance(force.getCutoffDistance());
            NonbondedForceImpl::calcPMEParameters(system, nb, alpha, gridSizeX, gridSizeY, gridSizeZ);
            gridSizeX = findFFTDimension(gridSizeX);
            gridSizeY = findFFTDimension(gridSizeY);
            gridSizeZ = findFFTDimension(gridSizeZ);
        } else {
            gridSizeX = pmeGridDimension[0];
            gridSizeY = pmeGridDimension[1];
            gridSizeZ = pmeGridDimension[2];
        }
        defines["EWALD_ALPHA"] = cu.doubleToString(alpha);
        defines["SQRT_PI"] = cu.doubleToString(sqrt(M_PI));
        defines["USE_EWALD"] = "";
        defines["USE_CUTOFF"] = "";
        defines["USE_PERIODIC"] = "";
        defines["CUTOFF_SQUARED"] = cu.doubleToString(force.getCutoffDistance()*force.getCutoffDistance());
    }
    CUmodule module = cu.createModule(CudaKernelSources::vectorOps+CudaAmoebaKernelSources::multipoles, defines);
    computeMomentsKernel = cu.getKernel(module, "computeLabFrameMoments");
    recordInducedDipolesKernel = cu.getKernel(module, "recordInducedDipoles");
    mapTorqueKernel = cu.getKernel(module, "mapTorqueToForce");
    computePotentialKernel = cu.getKernel(module, "computePotentialAtPoints");
    module = cu.createModule(CudaKernelSources::vectorOps+CudaAmoebaKernelSources::multipoleFixedField, defines);
    computeFixedFieldKernel = cu.getKernel(module, "computeFixedField");
    if (maxInducedIterations > 0) {
        module = cu.createModule(CudaKernelSources::vectorOps+CudaAmoebaKernelSources::multipoleInducedField, defines);
        computeInducedFieldKernel = cu.getKernel(module, "computeInducedField");
        updateInducedFieldKernel = cu.getKernel(module, "updateInducedFieldBySOR");
    }
    stringstream electrostaticsSource;
    if (usePME) {
        electrostaticsSource << CudaKernelSources::vectorOps;
        electrostaticsSource << CudaAmoebaKernelSources::pmeMultipoleElectrostatics;
        electrostaticsSource << CudaAmoebaKernelSources::pmeElectrostaticPairForce;
    }
    else {
        electrostaticsSource << CudaKernelSources::vectorOps;
        electrostaticsSource << CudaAmoebaKernelSources::multipoleElectrostatics;
        electrostaticsSource << "#define F1\n";
        electrostaticsSource << CudaAmoebaKernelSources::electrostaticPairForce;
        electrostaticsSource << "#undef F1\n";
        electrostaticsSource << "#define T1\n";
        electrostaticsSource << CudaAmoebaKernelSources::electrostaticPairForce;
        electrostaticsSource << "#undef T1\n";
        electrostaticsSource << "#define T2\n";
        electrostaticsSource << CudaAmoebaKernelSources::electrostaticPairForce;
    }
    module = cu.createModule(electrostaticsSource.str(), defines);
    electrostaticsKernel = cu.getKernel(module, "computeElectrostatics");

    // Set up PME.
    
    if (usePME) {
        // Create the PME kernels.

        map<string, string> pmeDefines;
        pmeDefines["EWALD_ALPHA"] = cu.doubleToString(alpha);
        pmeDefines["PME_ORDER"] = cu.intToString(PmeOrder);
        pmeDefines["NUM_ATOMS"] = cu.intToString(numMultipoles);
        pmeDefines["PADDED_NUM_ATOMS"] = cu.intToString(cu.getPaddedNumAtoms());
        pmeDefines["EPSILON_FACTOR"] = cu.doubleToString(138.9354558456);
        pmeDefines["GRID_SIZE_X"] = cu.intToString(gridSizeX);
        pmeDefines["GRID_SIZE_Y"] = cu.intToString(gridSizeY);
        pmeDefines["GRID_SIZE_Z"] = cu.intToString(gridSizeZ);
        pmeDefines["M_PI"] = cu.doubleToString(M_PI);
        pmeDefines["SQRT_PI"] = cu.doubleToString(sqrt(M_PI));
        CUmodule module = cu.createModule(CudaKernelSources::vectorOps+CudaAmoebaKernelSources::multipolePme, pmeDefines);
        pmeUpdateBsplinesKernel = cu.getKernel(module, "updateBsplines");
        pmeAtomRangeKernel = cu.getKernel(module, "findAtomRangeForGrid");
        pmeZIndexKernel = cu.getKernel(module, "recordZIndex");
        pmeSpreadFixedMultipolesKernel = cu.getKernel(module, "gridSpreadFixedMultipoles");
        pmeSpreadInducedDipolesKernel = cu.getKernel(module, "gridSpreadInducedDipoles");
        pmeConvolutionKernel = cu.getKernel(module, "reciprocalConvolution");
        pmeFixedPotentialKernel = cu.getKernel(module, "computeFixedPotentialFromGrid");
        pmeInducedPotentialKernel = cu.getKernel(module, "computeInducedPotentialFromGrid");
        pmeFixedForceKernel = cu.getKernel(module, "computeFixedMultipoleForceAndEnergy");
        pmeInducedForceKernel = cu.getKernel(module, "computeInducedDipoleForceAndEnergy");
        pmeRecordInducedFieldDipolesKernel = cu.getKernel(module, "recordInducedFieldDipoles");

        // Create required data structures.

        int elementSize = (cu.getUseDoublePrecision() ? sizeof(double) : sizeof(float));
        pmeGrid = new CudaArray(cu, gridSizeX*gridSizeY*gridSizeZ, 2*elementSize, "pmeGrid");
        cu.addAutoclearBuffer(*pmeGrid);
        pmeBsplineModuliX = new CudaArray(cu, gridSizeX, elementSize, "pmeBsplineModuliX");
        pmeBsplineModuliY = new CudaArray(cu, gridSizeY, elementSize, "pmeBsplineModuliY");
        pmeBsplineModuliZ = new CudaArray(cu, gridSizeZ, elementSize, "pmeBsplineModuliZ");
        pmeTheta1 = new CudaArray(cu, PmeOrder*numMultipoles, 4*elementSize, "pmeTheta1");
        pmeTheta2 = new CudaArray(cu, PmeOrder*numMultipoles, 4*elementSize, "pmeTheta2");
        pmeTheta3 = new CudaArray(cu, PmeOrder*numMultipoles, 4*elementSize, "pmeTheta3");
        pmeIgrid = CudaArray::create<int4>(cu, numMultipoles, "pmeIgrid");
        pmePhi = new CudaArray(cu, 20*numMultipoles, elementSize, "pmePhi");
        pmePhid = new CudaArray(cu, 10*numMultipoles, elementSize, "pmePhid");
        pmePhip = new CudaArray(cu, 10*numMultipoles, elementSize, "pmePhip");
        pmePhidp = new CudaArray(cu, 20*numMultipoles, elementSize, "pmePhidp");
        pmeAtomRange = CudaArray::create<int>(cu, gridSizeX*gridSizeY*gridSizeZ+1, "pmeAtomRange");
        pmeAtomGridIndex = CudaArray::create<int2>(cu, numMultipoles, "pmeAtomGridIndex");
        sort = new CudaSort(cu, new SortTrait(), cu.getNumAtoms());
        cufftResult result = cufftPlan3d(&fft, gridSizeX, gridSizeY, gridSizeZ, cu.getUseDoublePrecision() ? CUFFT_Z2Z : CUFFT_C2C);
        if (result != CUFFT_SUCCESS)
            throw OpenMMException("Error initializing FFT: "+cu.intToString(result));
        hasInitializedFFT = true;

        // Initialize the b-spline moduli.

        double data[PmeOrder];
        double x = 0.0;
        data[0] = 1.0 - x;
        data[1] = x;
        for (int i = 2; i < PmeOrder; i++) {
            double denom = 1.0/i;
            data[i] = x*data[i-1]*denom;
            for (int j = 1; j < i; j++)
                data[i-j] = ((x+j)*data[i-j-1] + ((i-j+1)-x)*data[i-j])*denom;
            data[0] = (1.0-x)*data[0]*denom;
        }
        int maxSize = max(max(gridSizeX, gridSizeY), gridSizeZ);
        vector<double> bsplines_data(maxSize+1, 0.0);
        for (int i = 2; i <= PmeOrder+1; i++)
            bsplines_data[i] = data[i-2];
        for (int dim = 0; dim < 3; dim++) {
            int ndata = (dim == 0 ? gridSizeX : dim == 1 ? gridSizeY : gridSizeZ);
            vector<double> moduli(ndata);

            // get the modulus of the discrete Fourier transform

            double factor = 2.0*M_PI/ndata;
            for (int i = 0; i < ndata; i++) {
                double sc = 0.0;
                double ss = 0.0;
                for (int j = 1; j <= ndata; j++) {
                    double arg = factor*i*(j-1);
                    sc += bsplines_data[j]*cos(arg);
                    ss += bsplines_data[j]*sin(arg);
                }
                moduli[i] = sc*sc+ss*ss;
            }

            // Fix for exponential Euler spline interpolation failure.

            double eps = 1.0e-7;
            if (moduli[0] < eps)
                moduli[0] = 0.9*moduli[1];
            for (int i = 1; i < ndata-1; i++)
                if (moduli[i] < eps)
                    moduli[i] = 0.9*(moduli[i-1]+moduli[i+1]);
            if (moduli[ndata-1] < eps)
                moduli[ndata-1] = 0.9*moduli[ndata-2];

            // Compute and apply the optimal zeta coefficient.

            int jcut = 50;
            for (int i = 1; i <= ndata; i++) {
                int k = i - 1;
                if (i > ndata/2)
                    k = k - ndata;
                double zeta;
                if (k == 0)
                    zeta = 1.0;
                else {
                    double sum1 = 1.0;
                    double sum2 = 1.0;
                    factor = M_PI*k/ndata;
                    for (int j = 1; j <= jcut; j++) {
                        double arg = factor/(factor+M_PI*j);
                        sum1 += pow(arg, PmeOrder);
                        sum2 += pow(arg, 2*PmeOrder);
                    }
                    for (int j = 1; j <= jcut; j++) {
                        double arg = factor/(factor-M_PI*j);
                        sum1 += pow(arg, PmeOrder);
                        sum2 += pow(arg, 2*PmeOrder);
                    }
                    zeta = sum2/sum1;
                }
                moduli[i-1] = moduli[i-1]*zeta*zeta;
            }
            if (cu.getUseDoublePrecision()) {
                if (dim == 0)
                    pmeBsplineModuliX->upload(moduli);
                else if (dim == 1)
                    pmeBsplineModuliY->upload(moduli);
                else
                    pmeBsplineModuliZ->upload(moduli);
            }
            else {
                vector<float> modulif(ndata);
                for (int i = 0; i < ndata; i++)
                    modulif[i] = (float) moduli[i];
                if (dim == 0)
                    pmeBsplineModuliX->upload(modulif);
                else if (dim == 1)
                    pmeBsplineModuliY->upload(modulif);
                else
                    pmeBsplineModuliZ->upload(modulif);
            }
        }
    }

    // Add an interaction to the default nonbonded kernel.  This doesn't actually do any calculations.  It's
    // just so that CudaNonbondedUtilities will build the exclusion flags and maintain the neighbor list.
    
    cu.getNonbondedUtilities().addInteraction(usePME, usePME, true, force.getCutoffDistance(), exclusions, "", force.getForceGroup());
    cu.addForce(new ForceInfo(force));
}

void CudaCalcAmoebaMultipoleForceKernel::initializeScaleFactors() {
    hasInitializedScaleFactors = true;
    CudaNonbondedUtilities& nb = cu.getNonbondedUtilities();
    
    // Figure out the covalent flag values to use for each atom pair.
    
    vector<unsigned int> exclusionIndices;
    vector<unsigned int> exclusionRowIndices;
    nb.getExclusionIndices().download(exclusionIndices);
    nb.getExclusionRowIndices().download(exclusionRowIndices);
    covalentFlags = CudaArray::create<uint2>(cu, nb.getExclusions().getSize(), "covalentFlags");
    vector<uint2> covalentFlagsVec(nb.getExclusions().getSize(), make_uint2(0, 0));
    for (int i = 0; i < (int) covalentFlagValues.size(); i++) {
        int atom1 = covalentFlagValues[i].x;
        int atom2 = covalentFlagValues[i].y;
        int value = covalentFlagValues[i].z;
        int x = atom1/CudaContext::TileSize;
        int offset1 = atom1-x*CudaContext::TileSize;
        int y = atom2/CudaContext::TileSize;
        int offset2 = atom2-y*CudaContext::TileSize;
        int f1 = (value == 0 || value == 1 ? 1 : 0);
        int f2 = (value == 0 || value == 2 ? 1 : 0);
        if (x > y) {
            int index = CudaNonbondedUtilities::findExclusionIndex(x, y, exclusionIndices, exclusionRowIndices);
            covalentFlagsVec[index+offset1].x |= f1<<offset2;
            covalentFlagsVec[index+offset1].y |= f2<<offset2;
        }
        else {
            int index = CudaNonbondedUtilities::findExclusionIndex(y, x, exclusionIndices, exclusionRowIndices);
            covalentFlagsVec[index+offset2].x |= f1<<offset1;
            covalentFlagsVec[index+offset2].y |= f2<<offset1;
        }
    }
    covalentFlags->upload(covalentFlagsVec);
    
    // Do the same for the polarization flags.
    
    polarizationGroupFlags = CudaArray::create<unsigned int>(cu, nb.getExclusions().getSize(), "polarizationGroupFlags");
    vector<unsigned int> polarizationGroupFlagsVec(nb.getExclusions().getSize(), 0);
    for (int i = 0; i < (int) polarizationFlagValues.size(); i++) {
        int atom1 = polarizationFlagValues[i].x;
        int atom2 = polarizationFlagValues[i].y;
        int x = atom1/CudaContext::TileSize;
        int offset1 = atom1-x*CudaContext::TileSize;
        int y = atom2/CudaContext::TileSize;
        int offset2 = atom2-y*CudaContext::TileSize;
        if (x > y) {
            int index = CudaNonbondedUtilities::findExclusionIndex(x, y, exclusionIndices, exclusionRowIndices);
            polarizationGroupFlagsVec[index+offset1] |= 1<<offset2;
        }
        else {
            int index = CudaNonbondedUtilities::findExclusionIndex(y, x, exclusionIndices, exclusionRowIndices);
            polarizationGroupFlagsVec[index+offset2] |= 1<<offset1;
        }
    }
    polarizationGroupFlags->upload(polarizationGroupFlagsVec);
}

double CudaCalcAmoebaMultipoleForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    if (!hasInitializedScaleFactors)
        initializeScaleFactors();
    CudaNonbondedUtilities& nb = cu.getNonbondedUtilities();
    
    // Compute the lab frame moments.

    void* computeMomentsArgs[] = {&cu.getPosq().getDevicePointer(), &multipoleParticles->getDevicePointer(),
        &molecularDipoles->getDevicePointer(), &molecularQuadrupoles->getDevicePointer(),
        &labFrameDipoles->getDevicePointer(), &labFrameQuadrupoles->getDevicePointer()};
    cu.executeKernel(computeMomentsKernel, computeMomentsArgs, cu.getNumAtoms());
    int startTileIndex = nb.getStartTileIndex();
    int numTileIndices = nb.getNumTiles();
    int numForceThreadBlocks = nb.getNumForceThreadBlocks();
    int forceThreadBlockSize = nb.getForceThreadBlockSize();
    int elementSize = (cu.getUseDoublePrecision() ? sizeof(double) : sizeof(float));
    if (pmeGrid == NULL) {
        // Compute induced dipoles.
        
        void* computeFixedFieldArgs[] = {&field->getDevicePointer(), &fieldPolar->getDevicePointer(), &cu.getPosq().getDevicePointer(),
            &nb.getExclusionIndices().getDevicePointer(), &nb.getExclusionRowIndices().getDevicePointer(),
            &covalentFlags->getDevicePointer(), &polarizationGroupFlags->getDevicePointer(), &startTileIndex, &numTileIndices,
            &labFrameDipoles->getDevicePointer(), &labFrameQuadrupoles->getDevicePointer(), &dampingAndThole->getDevicePointer()};
        cu.executeKernel(computeFixedFieldKernel, computeFixedFieldArgs, numForceThreadBlocks*forceThreadBlockSize, forceThreadBlockSize);
        void* recordInducedDipolesArgs[] = {&field->getDevicePointer(), &fieldPolar->getDevicePointer(),
            &inducedDipole->getDevicePointer(), &inducedDipolePolar->getDevicePointer(), &polarizability->getDevicePointer()};
        cu.executeKernel(recordInducedDipolesKernel, recordInducedDipolesArgs, cu.getNumAtoms());
        
        // Iterate until the dipoles converge.
        
        vector<float2> errors;
        for (int i = 0; i < maxInducedIterations; i++) {
            cu.clearBuffer(*inducedField);
            cu.clearBuffer(*inducedFieldPolar);
            void* computeInducedFieldArgs[] = {&inducedField->getDevicePointer(), &inducedFieldPolar->getDevicePointer(), &cu.getPosq().getDevicePointer(),
                &inducedDipole->getDevicePointer(), &inducedDipolePolar->getDevicePointer(), &startTileIndex, &numTileIndices,
                &dampingAndThole->getDevicePointer()};
            cu.executeKernel(computeInducedFieldKernel, computeInducedFieldArgs, numForceThreadBlocks*forceThreadBlockSize, forceThreadBlockSize);
            void* updateInducedFieldArgs[] = {&field->getDevicePointer(), &fieldPolar->getDevicePointer(), &inducedField->getDevicePointer(),
                &inducedFieldPolar->getDevicePointer(), &inducedDipole->getDevicePointer(), &inducedDipolePolar->getDevicePointer(),
                &polarizability->getDevicePointer(), &inducedDipoleErrors->getDevicePointer()};
            cu.executeKernel(updateInducedFieldKernel, updateInducedFieldArgs, cu.getNumThreadBlocks()*cu.ThreadBlockSize, cu.ThreadBlockSize, cu.ThreadBlockSize*elementSize*2);
            inducedDipoleErrors->download(errors);
            double total1 = 0.0, total2 = 0.0;
            for (int j = 0; j < (int) errors.size(); j++) {
                total1 += errors[j].x;
                total2 += errors[j].y;
            }
            if (48.033324*sqrt(max(total1, total2)/cu.getNumAtoms()) < inducedEpsilon)
                break;
        }
        
        // Compute electrostatic force.
        
        void* electrostaticsArgs[] = {&cu.getForce().getDevicePointer(), &torque->getDevicePointer(), &cu.getEnergyBuffer().getDevicePointer(),
            &cu.getPosq().getDevicePointer(), &nb.getExclusionIndices().getDevicePointer(), &nb.getExclusionRowIndices().getDevicePointer(),
            &covalentFlags->getDevicePointer(), &polarizationGroupFlags->getDevicePointer(), &startTileIndex, &numTileIndices,
            &labFrameDipoles->getDevicePointer(), &labFrameQuadrupoles->getDevicePointer(), &inducedDipole->getDevicePointer(),
            &inducedDipolePolar->getDevicePointer(), &dampingAndThole->getDevicePointer()};
        cu.executeKernel(electrostaticsKernel, electrostaticsArgs, numForceThreadBlocks*forceThreadBlockSize, forceThreadBlockSize);
        
        // Map torques to force.
        
        void* mapTorqueArgs[] = {&cu.getForce().getDevicePointer(), &torque->getDevicePointer(),
            &cu.getPosq().getDevicePointer(), &multipoleParticles->getDevicePointer()};
        cu.executeKernel(mapTorqueKernel, mapTorqueArgs, cu.getNumAtoms());
    }
    else {
        // Reciprocal space calculation.
        
        unsigned int maxTiles = nb.getInteractingTiles().getSize();
        void* pmeUpdateBsplinesArgs[] = {&cu.getPosq().getDevicePointer(), &pmeIgrid->getDevicePointer(), &pmeAtomGridIndex->getDevicePointer(),
            &pmeTheta1->getDevicePointer(), &pmeTheta2->getDevicePointer(), &pmeTheta3->getDevicePointer(), cu.getPeriodicBoxSizePointer(),
            cu.getInvPeriodicBoxSizePointer()};
        cu.executeKernel(pmeUpdateBsplinesKernel, pmeUpdateBsplinesArgs, cu.getNumAtoms(), cu.ThreadBlockSize, cu.ThreadBlockSize*PmeOrder*PmeOrder*elementSize);
        sort->sort(*pmeAtomGridIndex);
        void* pmeAtomRangeArgs[] = {&pmeAtomGridIndex->getDevicePointer(), &pmeAtomRange->getDevicePointer(),
            &cu.getPosq().getDevicePointer(), cu.getPeriodicBoxSizePointer(), cu.getInvPeriodicBoxSizePointer()};
        cu.executeKernel(pmeAtomRangeKernel, pmeAtomRangeArgs, cu.getNumAtoms());
        void* pmeZIndexArgs[] = {&pmeAtomGridIndex->getDevicePointer(), &cu.getPosq().getDevicePointer(), cu.getPeriodicBoxSizePointer(), cu.getInvPeriodicBoxSizePointer()};
        cu.executeKernel(pmeZIndexKernel, pmeZIndexArgs, cu.getNumAtoms());
        void* pmeSpreadFixedMultipolesArgs[] = {&cu.getPosq().getDevicePointer(), &labFrameDipoles->getDevicePointer(), &labFrameQuadrupoles->getDevicePointer(),
            &pmeGrid->getDevicePointer(), &pmeAtomGridIndex->getDevicePointer(), &pmeAtomRange->getDevicePointer(),
            &pmeTheta1->getDevicePointer(), &pmeTheta2->getDevicePointer(), &pmeTheta3->getDevicePointer(), cu.getInvPeriodicBoxSizePointer()};
        cu.executeKernel(pmeSpreadFixedMultipolesKernel, pmeSpreadFixedMultipolesArgs, cu.getNumAtoms());
        if (cu.getUseDoublePrecision())
            cufftExecZ2Z(fft, (double2*) pmeGrid->getDevicePointer(), (double2*) pmeGrid->getDevicePointer(), CUFFT_FORWARD);
        else
            cufftExecC2C(fft, (float2*) pmeGrid->getDevicePointer(), (float2*) pmeGrid->getDevicePointer(), CUFFT_FORWARD);
        void* pmeConvolutionArgs[] = {&pmeGrid->getDevicePointer(), &pmeBsplineModuliX->getDevicePointer(), &pmeBsplineModuliY->getDevicePointer(),
            &pmeBsplineModuliZ->getDevicePointer(), cu.getPeriodicBoxSizePointer(), cu.getInvPeriodicBoxSizePointer()};
        cu.executeKernel(pmeConvolutionKernel, pmeConvolutionArgs, cu.getNumAtoms());
        if (cu.getUseDoublePrecision())
            cufftExecZ2Z(fft, (double2*) pmeGrid->getDevicePointer(), (double2*) pmeGrid->getDevicePointer(), CUFFT_INVERSE);
        else
            cufftExecC2C(fft, (float2*) pmeGrid->getDevicePointer(), (float2*) pmeGrid->getDevicePointer(), CUFFT_INVERSE);
        void* pmeFixedPotentialArgs[] = {&pmeGrid->getDevicePointer(), &pmePhi->getDevicePointer(), &field->getDevicePointer(),
            &fieldPolar ->getDevicePointer(), &pmeIgrid->getDevicePointer(), &pmeTheta1->getDevicePointer(), &pmeTheta2->getDevicePointer(),
            &pmeTheta3->getDevicePointer(), &labFrameDipoles->getDevicePointer(), cu.getInvPeriodicBoxSizePointer()};
        cu.executeKernel(pmeFixedPotentialKernel, pmeFixedPotentialArgs, cu.getNumAtoms());
        void* pmeFixedForceArgs[] = {&cu.getPosq().getDevicePointer(), &cu.getForce().getDevicePointer(), &torque->getDevicePointer(),
            &cu.getEnergyBuffer().getDevicePointer(), &labFrameDipoles->getDevicePointer(), &labFrameQuadrupoles->getDevicePointer(),
            &pmePhi->getDevicePointer(), cu.getInvPeriodicBoxSizePointer()};
        cu.executeKernel(pmeFixedForceKernel, pmeFixedForceArgs, cu.getNumAtoms());
        
        // Direct space calculation.
        
        void* computeFixedFieldArgs[] = {&field->getDevicePointer(), &fieldPolar->getDevicePointer(), &cu.getPosq().getDevicePointer(),
            &nb.getExclusionIndices().getDevicePointer(), &nb.getExclusionRowIndices().getDevicePointer(),
            &covalentFlags->getDevicePointer(), &polarizationGroupFlags->getDevicePointer(), &startTileIndex, &numTileIndices,
            &nb.getInteractingTiles().getDevicePointer(), &nb.getInteractionCount().getDevicePointer(), cu.getPeriodicBoxSizePointer(),
            cu.getInvPeriodicBoxSizePointer(), &maxTiles, &nb.getInteractionFlags().getDevicePointer(),
            &labFrameDipoles->getDevicePointer(), &labFrameQuadrupoles->getDevicePointer(), &dampingAndThole->getDevicePointer()};
        cu.executeKernel(computeFixedFieldKernel, computeFixedFieldArgs, numForceThreadBlocks*forceThreadBlockSize, forceThreadBlockSize);
        void* recordInducedDipolesArgs[] = {&field->getDevicePointer(), &fieldPolar->getDevicePointer(),
            &inducedDipole->getDevicePointer(), &inducedDipolePolar->getDevicePointer(), &polarizability->getDevicePointer()};
        cu.executeKernel(recordInducedDipolesKernel, recordInducedDipolesArgs, cu.getNumAtoms());

        // Reciprocal space calculation for the induced dipoles.

        void* pmeSpreadInducedDipolesArgs[] = {&cu.getPosq().getDevicePointer(), &inducedDipole->getDevicePointer(), &inducedDipolePolar->getDevicePointer(),
            &pmeGrid->getDevicePointer(), &pmeAtomGridIndex->getDevicePointer(), &pmeAtomRange->getDevicePointer(),
            &pmeTheta1->getDevicePointer(), &pmeTheta2->getDevicePointer(), &pmeTheta3->getDevicePointer(), cu.getInvPeriodicBoxSizePointer()};
        cu.executeKernel(pmeSpreadInducedDipolesKernel, pmeSpreadInducedDipolesArgs, cu.getNumAtoms());
        if (cu.getUseDoublePrecision())
            cufftExecZ2Z(fft, (double2*) pmeGrid->getDevicePointer(), (double2*) pmeGrid->getDevicePointer(), CUFFT_FORWARD);
        else
            cufftExecC2C(fft, (float2*) pmeGrid->getDevicePointer(), (float2*) pmeGrid->getDevicePointer(), CUFFT_FORWARD);
        cu.executeKernel(pmeConvolutionKernel, pmeConvolutionArgs, cu.getNumAtoms());
        if (cu.getUseDoublePrecision())
            cufftExecZ2Z(fft, (double2*) pmeGrid->getDevicePointer(), (double2*) pmeGrid->getDevicePointer(), CUFFT_INVERSE);
        else
            cufftExecC2C(fft, (float2*) pmeGrid->getDevicePointer(), (float2*) pmeGrid->getDevicePointer(), CUFFT_INVERSE);
        void* pmeInducedPotentialArgs[] = {&pmeGrid->getDevicePointer(), &pmePhid->getDevicePointer(), &pmePhip->getDevicePointer(),
            &pmePhidp->getDevicePointer(), &pmeIgrid->getDevicePointer(), &pmeTheta1->getDevicePointer(), &pmeTheta2->getDevicePointer(),
            &pmeTheta3->getDevicePointer(), cu.getInvPeriodicBoxSizePointer()};
        cu.executeKernel(pmeInducedPotentialKernel, pmeInducedPotentialArgs, cu.getNumAtoms());
        
        // Iterate until the dipoles converge.
        
        vector<float2> errors;
        for (int i = 0; i < maxInducedIterations; i++) {
            cu.clearBuffer(*inducedField);
            cu.clearBuffer(*inducedFieldPolar);
            void* computeInducedFieldArgs[] = {&inducedField->getDevicePointer(), &inducedFieldPolar->getDevicePointer(), &cu.getPosq().getDevicePointer(),
                &inducedDipole->getDevicePointer(), &inducedDipolePolar->getDevicePointer(), &startTileIndex, &numTileIndices,
                &nb.getInteractingTiles().getDevicePointer(), &nb.getInteractionCount().getDevicePointer(), cu.getPeriodicBoxSizePointer(),
                cu.getInvPeriodicBoxSizePointer(), &maxTiles, &nb.getInteractionFlags().getDevicePointer(),
                &dampingAndThole->getDevicePointer()};
            cu.executeKernel(computeInducedFieldKernel, computeInducedFieldArgs, numForceThreadBlocks*forceThreadBlockSize, forceThreadBlockSize);
            cu.executeKernel(pmeSpreadInducedDipolesKernel, pmeSpreadInducedDipolesArgs, cu.getNumAtoms());
            if (cu.getUseDoublePrecision())
                cufftExecZ2Z(fft, (double2*) pmeGrid->getDevicePointer(), (double2*) pmeGrid->getDevicePointer(), CUFFT_FORWARD);
            else
                cufftExecC2C(fft, (float2*) pmeGrid->getDevicePointer(), (float2*) pmeGrid->getDevicePointer(), CUFFT_FORWARD);
            cu.executeKernel(pmeConvolutionKernel, pmeConvolutionArgs, cu.getNumAtoms());
            if (cu.getUseDoublePrecision())
                cufftExecZ2Z(fft, (double2*) pmeGrid->getDevicePointer(), (double2*) pmeGrid->getDevicePointer(), CUFFT_INVERSE);
            else
                cufftExecC2C(fft, (float2*) pmeGrid->getDevicePointer(), (float2*) pmeGrid->getDevicePointer(), CUFFT_INVERSE);
            cu.executeKernel(pmeInducedPotentialKernel, pmeInducedPotentialArgs, cu.getNumAtoms());
            void* pmeRecordInducedFieldDipolesArgs[] = {&pmePhid->getDevicePointer(), &pmePhip->getDevicePointer(),
                &inducedField->getDevicePointer(), &inducedFieldPolar->getDevicePointer(), cu.getInvPeriodicBoxSizePointer()};
            cu.executeKernel(pmeRecordInducedFieldDipolesKernel, pmeRecordInducedFieldDipolesArgs, cu.getNumAtoms());
            void* updateInducedFieldArgs[] = {&field->getDevicePointer(), &fieldPolar->getDevicePointer(), &inducedField->getDevicePointer(),
                &inducedFieldPolar->getDevicePointer(), &inducedDipole->getDevicePointer(), &inducedDipolePolar->getDevicePointer(),
                &polarizability->getDevicePointer(), &inducedDipoleErrors->getDevicePointer()};
            cu.executeKernel(updateInducedFieldKernel, updateInducedFieldArgs, cu.getNumThreadBlocks()*cu.ThreadBlockSize, cu.ThreadBlockSize, cu.ThreadBlockSize*elementSize*2);
            inducedDipoleErrors->download(errors);
            double total1 = 0.0, total2 = 0.0;
            for (int j = 0; j < (int) errors.size(); j++) {
                total1 += errors[j].x;
                total2 += errors[j].y;
            }
            if (48.033324*sqrt(max(total1, total2)/cu.getNumAtoms()) < inducedEpsilon)
                break;
        }
        
        // Compute electrostatic force.
        
        void* electrostaticsArgs[] = {&cu.getForce().getDevicePointer(), &torque->getDevicePointer(), &cu.getEnergyBuffer().getDevicePointer(),
            &cu.getPosq().getDevicePointer(), &nb.getExclusionIndices().getDevicePointer(), &nb.getExclusionRowIndices().getDevicePointer(),
            &covalentFlags->getDevicePointer(), &polarizationGroupFlags->getDevicePointer(), &startTileIndex, &numTileIndices,
            &nb.getInteractingTiles().getDevicePointer(), &nb.getInteractionCount().getDevicePointer(),
            cu.getPeriodicBoxSizePointer(), cu.getInvPeriodicBoxSizePointer(), &maxTiles, &nb.getInteractionFlags().getDevicePointer(),
            &labFrameDipoles->getDevicePointer(), &labFrameQuadrupoles->getDevicePointer(), &inducedDipole->getDevicePointer(),
            &inducedDipolePolar->getDevicePointer(), &dampingAndThole->getDevicePointer()};
        cu.executeKernel(electrostaticsKernel, electrostaticsArgs, numForceThreadBlocks*forceThreadBlockSize, forceThreadBlockSize);
        void* pmeInducedForceArgs[] = {&cu.getPosq().getDevicePointer(), &cu.getForce().getDevicePointer(), &torque->getDevicePointer(),
            &cu.getEnergyBuffer().getDevicePointer(), &labFrameDipoles->getDevicePointer(), &labFrameQuadrupoles->getDevicePointer(),
            &inducedDipole->getDevicePointer(), &inducedDipolePolar->getDevicePointer(), &pmePhi->getDevicePointer(), &pmePhid->getDevicePointer(),
            &pmePhip->getDevicePointer(), &pmePhidp->getDevicePointer(), cu.getInvPeriodicBoxSizePointer()};
        cu.executeKernel(pmeInducedForceKernel, pmeInducedForceArgs, cu.getNumAtoms());
        
        // Map torques to force.
        
        void* mapTorqueArgs[] = {&cu.getForce().getDevicePointer(), &torque->getDevicePointer(),
            &cu.getPosq().getDevicePointer(), &multipoleParticles->getDevicePointer()};
        cu.executeKernel(mapTorqueKernel, mapTorqueArgs, cu.getNumAtoms());
    }
    return 0.0;
}

void CudaCalcAmoebaMultipoleForceKernel::getElectrostaticPotential(ContextImpl& context, const vector<Vec3>& inputGrid, vector<double>& outputElectrostaticPotential) {
    context.calcForcesAndEnergy(false, false, -1);
    int numPoints = inputGrid.size();
    int elementSize = (cu.getUseDoublePrecision() ? sizeof(double) : sizeof(float));
    CudaArray points(cu, numPoints, 4*elementSize, "points");
    CudaArray potential(cu, numPoints, elementSize, "potential");
    
    // Copy the grid points to the GPU.
    
    if (cu.getUseDoublePrecision()) {
        vector<double4> p(numPoints);
        for (int i = 0; i < numPoints; i++)
            p[i] = make_double4(inputGrid[i][0], inputGrid[i][1], inputGrid[i][2], 0);
        points.upload(p);
    }
    else {
        vector<float4> p(numPoints);
        for (int i = 0; i < numPoints; i++)
            p[i] = make_float4((float) inputGrid[i][0], (float) inputGrid[i][1], (float) inputGrid[i][2], 0);
        points.upload(p);
    }
    
    // Compute the potential.
    
    void* computePotentialArgs[] = {&cu.getPosq().getDevicePointer(), &labFrameDipoles->getDevicePointer(),
        &labFrameQuadrupoles->getDevicePointer(), &inducedDipole->getDevicePointer(), &points.getDevicePointer(),
        &potential.getDevicePointer(), &numPoints, cu.getPeriodicBoxSizePointer(), cu.getInvPeriodicBoxSizePointer()};
    int blockSize = 128;
    cu.executeKernel(computePotentialKernel, computePotentialArgs, numPoints, blockSize, blockSize*15*elementSize);
    outputElectrostaticPotential.resize(numPoints);
    if (cu.getUseDoublePrecision())
        potential.download(outputElectrostaticPotential);
    else {
        vector<float> p(numPoints);
        potential.download(p);
        for (int i = 0; i < numPoints; i++)
            outputElectrostaticPotential[i] = p[i];
    }
}

template <class T, class T4>
void CudaCalcAmoebaMultipoleForceKernel::computeSystemMultipoleMoments(ContextImpl& context, const Vec3& origin, vector<double>& outputMultipoleMoments) {
    // Compute the local coordinates relative to the center of mass.
    int numAtoms = cu.getNumAtoms();
    vector<T4> posq, velm;
    cu.getPosq().download(posq);
    cu.getVelm().download(velm);
    double totalMass = 0.0;
    Vec3 centerOfMass(0, 0, 0);
    for (int i = 0; i < numAtoms; i++) {
        double mass = (velm[i].w > 0 ? 1.0/velm[i].w : 0.0);
        totalMass += mass;
        centerOfMass[0] += mass*posq[i].x;
        centerOfMass[1] += mass*posq[i].y;
        centerOfMass[2] += mass*posq[i].z;
    }
    if (totalMass > 0.0) {
        centerOfMass[0] /= totalMass;
        centerOfMass[1] /= totalMass;
        centerOfMass[2] /= totalMass;
    }
    vector<double4> posqLocal(numAtoms);
    for (int i = 0; i < numAtoms; i++) {
        posqLocal[i].x = posq[i].x - centerOfMass[0];
        posqLocal[i].y = posq[i].y - centerOfMass[1];
        posqLocal[i].z = posq[i].z - centerOfMass[2];
        posqLocal[i].w = posq[i].w;
    }

    // Compute the multipole moments.
    
    double totalCharge = 0.0;
    double xdpl = 0.0;
    double ydpl = 0.0;
    double zdpl = 0.0;
    double xxqdp = 0.0;
    double xyqdp = 0.0;
    double xzqdp = 0.0;
    double yxqdp = 0.0;
    double yyqdp = 0.0;
    double yzqdp = 0.0;
    double zxqdp = 0.0;
    double zyqdp = 0.0;
    double zzqdp = 0.0;
    vector<T> labDipoleVec, inducedDipoleVec, quadrupoleVec;
    labFrameDipoles->download(labDipoleVec);
    inducedDipole->download(inducedDipoleVec);
    labFrameQuadrupoles->download(quadrupoleVec);
    for (int i = 0; i < numAtoms; i++) {
        totalCharge += posqLocal[i].w;
        double netDipoleX = (labDipoleVec[3*i]    + inducedDipoleVec[3*i]);
        double netDipoleY = (labDipoleVec[3*i+1]  + inducedDipoleVec[3*i+1]);
        double netDipoleZ = (labDipoleVec[3*i+2]  + inducedDipoleVec[3*i+2]);
        xdpl += posqLocal[i].x*posqLocal[i].w + netDipoleX;
        ydpl += posqLocal[i].y*posqLocal[i].w + netDipoleY;
        zdpl += posqLocal[i].z*posqLocal[i].w + netDipoleZ;
        xxqdp += posqLocal[i].x*posqLocal[i].x*posqLocal[i].w + 2*posqLocal[i].x*netDipoleX;
        xyqdp += posqLocal[i].x*posqLocal[i].y*posqLocal[i].w + posqLocal[i].x*netDipoleY + posqLocal[i].y*netDipoleX;
        xzqdp += posqLocal[i].x*posqLocal[i].z*posqLocal[i].w + posqLocal[i].x*netDipoleZ + posqLocal[i].z*netDipoleX;
        yxqdp += posqLocal[i].y*posqLocal[i].x*posqLocal[i].w + posqLocal[i].y*netDipoleX + posqLocal[i].x*netDipoleY;
        yyqdp += posqLocal[i].y*posqLocal[i].y*posqLocal[i].w + 2*posqLocal[i].y*netDipoleY;
        yzqdp += posqLocal[i].y*posqLocal[i].z*posqLocal[i].w + posqLocal[i].y*netDipoleZ + posqLocal[i].z*netDipoleY;
        zxqdp += posqLocal[i].z*posqLocal[i].x*posqLocal[i].w + posqLocal[i].z*netDipoleX + posqLocal[i].x*netDipoleZ;
        zyqdp += posqLocal[i].z*posqLocal[i].y*posqLocal[i].w + posqLocal[i].z*netDipoleY + posqLocal[i].y*netDipoleZ;
        zzqdp += posqLocal[i].z*posqLocal[i].z*posqLocal[i].w + 2*posqLocal[i].z*netDipoleZ;
    }

    // Convert the quadrupole from traced to traceless form.
 
    double qave = (xxqdp + yyqdp + zzqdp)/3;
    xxqdp = 1.5*(xxqdp-qave);
    xyqdp = 1.5*xyqdp;
    xzqdp = 1.5*xzqdp;
    yxqdp = 1.5*yxqdp;
    yyqdp = 1.5*(yyqdp-qave);
    yzqdp = 1.5*yzqdp;
    zxqdp = 1.5*zxqdp;
    zyqdp = 1.5*zyqdp;
    zzqdp = 1.5*(zzqdp-qave);

    // Add the traceless atomic quadrupoles to the total quadrupole moment.

    for (int i = 0; i < numAtoms; i++) {
        xxqdp = xxqdp + 3*quadrupoleVec[5*i];
        xyqdp = xyqdp + 3*quadrupoleVec[5*i+1];
        xzqdp = xzqdp + 3*quadrupoleVec[5*i+2];
        yxqdp = yxqdp + 3*quadrupoleVec[5*i+1];
        yyqdp = yyqdp + 3*quadrupoleVec[5*i+3];
        yzqdp = yzqdp + 3*quadrupoleVec[5*i+4];
        zxqdp = zxqdp + 3*quadrupoleVec[5*i+2];
        zyqdp = zyqdp + 3*quadrupoleVec[5*i+4];
        zzqdp = zzqdp + -3*(quadrupoleVec[5*i]+quadrupoleVec[5*i+3]);
    }
 
    double debye = 4.80321;
    outputMultipoleMoments.resize(13);
    outputMultipoleMoments[0] = totalCharge;
    outputMultipoleMoments[1] = xdpl*debye;
    outputMultipoleMoments[2] = ydpl*debye;
    outputMultipoleMoments[3] = zdpl*debye;
    outputMultipoleMoments[4] = xxqdp*debye;
    outputMultipoleMoments[5] = xyqdp*debye;
    outputMultipoleMoments[6] = xzqdp*debye;
    outputMultipoleMoments[7] = yxqdp*debye;
    outputMultipoleMoments[8] = yyqdp*debye;
    outputMultipoleMoments[9] = yzqdp*debye;
    outputMultipoleMoments[10] = zxqdp*debye;
    outputMultipoleMoments[11] = zyqdp*debye;
    outputMultipoleMoments[12] = zzqdp*debye;
}

void CudaCalcAmoebaMultipoleForceKernel::getSystemMultipoleMoments(ContextImpl& context, const Vec3& origin, vector<double>& outputMultipoleMoments) {
    context.calcForcesAndEnergy(false, false, -1);
    if (cu.getUseDoublePrecision())
        computeSystemMultipoleMoments<double, double4>(context, origin, outputMultipoleMoments);
    else
        computeSystemMultipoleMoments<float, float4>(context, origin, outputMultipoleMoments);
}

///* -------------------------------------------------------------------------- *
// *                       AmoebaGeneralizedKirkwood                            *
// * -------------------------------------------------------------------------- */
//
//class CudaCalcAmoebaGeneralizedKirkwoodForceKernel::ForceInfo : public CudaForceInfo {
//public:
//    ForceInfo(const AmoebaGeneralizedKirkwoodForce& force) : force(force) {
//    }
//    bool areParticlesIdentical(int particle1, int particle2) {
//        double charge1, charge2, radius1, radius2, scale1, scale2;
//        force.getParticleParameters(particle1, charge1, radius1, scale1);
//        force.getParticleParameters(particle2, charge2, radius2, scale2);
//        return (charge1 == charge2 && radius1 == radius2 && scale1 == scale2);
//    }
//private:
//    const AmoebaGeneralizedKirkwoodForce& force;
//};
//
//CudaCalcAmoebaGeneralizedKirkwoodForceKernel::CudaCalcAmoebaGeneralizedKirkwoodForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) : 
//           CalcAmoebaGeneralizedKirkwoodForceKernel(name, platform), cu(cu), system(system) {
//    data.incrementKernelCount();
//}
//
//CudaCalcAmoebaGeneralizedKirkwoodForceKernel::~CudaCalcAmoebaGeneralizedKirkwoodForceKernel() {
//    data.decrementKernelCount();
//}
//
//void CudaCalcAmoebaGeneralizedKirkwoodForceKernel::initialize(const System& system, const AmoebaGeneralizedKirkwoodForce& force) {
//
//    data.setHasAmoebaGeneralizedKirkwood( true );
//
//    int numParticles = system.getNumParticles();
//
//    std::vector<float> radius(numParticles);
//    std::vector<float> scale(numParticles);
//    std::vector<float> charge(numParticles);
//
//    for( int ii = 0; ii < numParticles; ii++ ){
//        double particleCharge, particleRadius, scalingFactor;
//        force.getParticleParameters(ii, particleCharge, particleRadius, scalingFactor);
//        radius[ii]  = static_cast<float>( particleRadius );
//        scale[ii]   = static_cast<float>( scalingFactor );
//        charge[ii]  = static_cast<float>( particleCharge );
//    }   
//    if( data.getUseGrycuk() ){
//
//        gpuSetAmoebaGrycukParameters( data.getAmoebaGpu(), static_cast<float>(force.getSoluteDielectric() ), 
//                                      static_cast<float>( force.getSolventDielectric() ), 
//                                      radius, scale, charge,
//                                      force.getIncludeCavityTerm(),
//                                      static_cast<float>( force.getProbeRadius() ), 
//                                      static_cast<float>( force.getSurfaceAreaFactor() ) ); 
//        
//    } else {
//
//        gpuSetAmoebaObcParameters( data.getAmoebaGpu(), static_cast<float>(force.getSoluteDielectric() ), 
//                                   static_cast<float>( force.getSolventDielectric() ), 
//                                   radius, scale, charge,
//                                   force.getIncludeCavityTerm(),
//                                   static_cast<float>( force.getProbeRadius() ), 
//                                   static_cast<float>( force.getSurfaceAreaFactor() ) ); 
//
//    }
//    data.getAmoebaGpu()->gpuContext->forces.push_back(new ForceInfo(force));
//}
//
//double CudaCalcAmoebaGeneralizedKirkwoodForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
//    // handled in computeAmoebaMultipoleForce()
//    return 0.0;
//}
//
//static void computeAmoebaVdwForce( CudaContext& cu ) {
//
//    amoebaGpuContext gpu = data.getAmoebaGpu();
//    data.initializeGpu();
//
//    // Vdw14_7F
//    kCalculateAmoebaVdw14_7Forces(gpu, data.getUseVdwNeighborList());
//}

/* -------------------------------------------------------------------------- *
 *                           AmoebaVdw                                        *
 * -------------------------------------------------------------------------- */

class CudaCalcAmoebaVdwForceKernel::ForceInfo : public CudaForceInfo {
public:
    ForceInfo(const AmoebaVdwForce& force) : force(force) {
    }
    bool areParticlesIdentical(int particle1, int particle2) {
        int iv1, iv2, class1, class2;
        double sigma1, sigma2, epsilon1, epsilon2, reduction1, reduction2;
        force.getParticleParameters(particle1, iv1, class1, sigma1, epsilon1, reduction1);
        force.getParticleParameters(particle2, iv2, class2, sigma2, epsilon2, reduction2);
        return (class1 == class2 && sigma1 == sigma2 && epsilon1 == epsilon2 && reduction1 == reduction2);
    }
private:
    const AmoebaVdwForce& force;
};

CudaCalcAmoebaVdwForceKernel::CudaCalcAmoebaVdwForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) :
        CalcAmoebaVdwForceKernel(name, platform), cu(cu), system(system), hasInitializedNonbonded(false), sigmaEpsilon(NULL),
        bondReductionAtoms(NULL), bondReductionFactors(NULL), tempPosq(NULL), tempForces(NULL), nonbonded(NULL) {
}

CudaCalcAmoebaVdwForceKernel::~CudaCalcAmoebaVdwForceKernel() {
    cu.setAsCurrent();
    if (sigmaEpsilon != NULL)
        delete sigmaEpsilon;
    if (bondReductionAtoms != NULL)
        delete bondReductionAtoms;
    if (bondReductionFactors != NULL)
        delete bondReductionFactors;
    if (tempPosq != NULL)
        delete tempPosq;
    if (tempForces != NULL)
        delete tempForces;
    if (nonbonded != NULL)
        delete nonbonded;
}

void CudaCalcAmoebaVdwForceKernel::initialize(const System& system, const AmoebaVdwForce& force) {
    cu.setAsCurrent();
    sigmaEpsilon = CudaArray::create<float2>(cu, cu.getPaddedNumAtoms(), "sigmaEpsilon");
    bondReductionAtoms = CudaArray::create<int>(cu, cu.getPaddedNumAtoms(), "bondReductionAtoms");
    bondReductionFactors = CudaArray::create<float>(cu, cu.getPaddedNumAtoms(), "sigmaEpsilon");
    tempPosq = new CudaArray(cu, cu.getPaddedNumAtoms(), cu.getUseDoublePrecision() ? sizeof(double4) : sizeof(float4), "tempPosq");
    tempForces = CudaArray::create<long long>(cu, 3*cu.getPaddedNumAtoms(), "tempForces");
    
    // Record atom parameters.
    
    vector<float2> sigmaEpsilonVec(cu.getPaddedNumAtoms(), make_float2(0, 1));
    vector<int> bondReductionAtomsVec(cu.getPaddedNumAtoms(), 0);
    vector<float> bondReductionFactorsVec(cu.getPaddedNumAtoms(), 0);
    vector<vector<int> > exclusions(cu.getNumAtoms());
    for (int i = 0; i < force.getNumParticles(); i++) {
        int ivIndex, classIndex;
        double sigma, epsilon, reductionFactor;
        force.getParticleParameters(i, ivIndex, classIndex, sigma, epsilon, reductionFactor);
        sigmaEpsilonVec[i] = make_float2((float) sigma, (float) epsilon);
        bondReductionAtomsVec[i] = ivIndex;
        bondReductionFactorsVec[i] = (float) reductionFactor;
        force.getParticleExclusions(i, exclusions[i]);
        exclusions[i].push_back(i);
    }
    sigmaEpsilon->upload(sigmaEpsilonVec);
    bondReductionAtoms->upload(bondReductionAtomsVec);
    bondReductionFactors->upload(bondReductionFactorsVec);
    if (force.getUseDispersionCorrection())
        dispersionCoefficient = AmoebaVdwForceImpl::calcDispersionCorrection(system, force);
    else
        dispersionCoefficient = 0.0;               
 
    // This force is applied based on modified atom positions, where hydrogens have been moved slightly
    // closer to their parent atoms.  We therefore create a separate CudaNonbondedUtilities just for
    // this force, so it will have its own neighbor list and interaction kernel.
    
    nonbonded = new CudaNonbondedUtilities(cu);
    nonbonded->addParameter(CudaNonbondedUtilities::ParameterInfo("sigmaEpsilon", "float", 2, sizeof(float2), sigmaEpsilon->getDevicePointer()));
    
    // Create the interaction kernel.
    
    map<string, string> replacements;
    string sigmaCombiningRule = force.getSigmaCombiningRule();
    if (sigmaCombiningRule == "ARITHMETIC")
        replacements["SIGMA_COMBINING_RULE"] = "1";
    else if (sigmaCombiningRule == "GEOMETRIC")
        replacements["SIGMA_COMBINING_RULE"] = "2";
    else if (sigmaCombiningRule == "CUBIC-MEAN")
        replacements["SIGMA_COMBINING_RULE"] = "3";
    else
        throw OpenMMException("Illegal combining rule for sigma: "+sigmaCombiningRule);
    string epsilonCombiningRule = force.getEpsilonCombiningRule();
    if (epsilonCombiningRule == "ARITHMETIC")
        replacements["EPSILON_COMBINING_RULE"] = "1";
    else if (epsilonCombiningRule == "GEOMETRIC")
        replacements["EPSILON_COMBINING_RULE"] = "2";
    else if (epsilonCombiningRule == "HARMONIC")
        replacements["EPSILON_COMBINING_RULE"] = "3";
    else if (epsilonCombiningRule == "HHG")
        replacements["EPSILON_COMBINING_RULE"] = "4";
    else
        throw OpenMMException("Illegal combining rule for sigma: "+sigmaCombiningRule);
    double cutoff = force.getCutoff();
    double taperCutoff = cutoff*0.9;
    replacements["CUTOFF_DISTANCE"] = cu.doubleToString(force.getCutoff());
    replacements["TAPER_CUTOFF"] = cu.doubleToString(taperCutoff);
    replacements["TAPER_C3"] = cu.doubleToString(10/pow(taperCutoff-cutoff, 3.0));
    replacements["TAPER_C4"] = cu.doubleToString(15/pow(taperCutoff-cutoff, 4.0));
    replacements["TAPER_C5"] = cu.doubleToString(6/pow(taperCutoff-cutoff, 5.0));
    nonbonded->addInteraction(force.getUseNeighborList(), force.getPBC(), true, force.getCutoff(), exclusions,
        cu.replaceStrings(CudaAmoebaKernelSources::amoebaVdwForce2, replacements), force.getForceGroup());
    
    // Create the other kernels.
    
    map<string, string> defines;
    defines["PADDED_NUM_ATOMS"] = cu.intToString(cu.getPaddedNumAtoms());
    CUmodule module = cu.createModule(CudaAmoebaKernelSources::amoebaVdwForce1, defines);
    prepareKernel = cu.getKernel(module, "prepareToComputeForce");
    spreadKernel = cu.getKernel(module, "spreadForces");
    cu.addForce(new ForceInfo(force));
}

double CudaCalcAmoebaVdwForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
    if (!hasInitializedNonbonded) {
        hasInitializedNonbonded = true;
        nonbonded->initialize(system);
    }
    cu.getPosq().copyTo(*tempPosq);
    cu.getForce().copyTo(*tempForces);
    void* prepareArgs[] = {&cu.getForce().getDevicePointer(), &cu.getPosq().getDevicePointer(), &tempPosq->getDevicePointer(),
        &bondReductionAtoms->getDevicePointer(), &bondReductionFactors->getDevicePointer()};
    cu.executeKernel(prepareKernel, prepareArgs, cu.getPaddedNumAtoms());
    nonbonded->prepareInteractions();
    nonbonded->computeInteractions();
    void* spreadArgs[] = {&cu.getForce().getDevicePointer(), &tempForces->getDevicePointer(), &bondReductionAtoms->getDevicePointer(), &bondReductionFactors->getDevicePointer()};
    cu.executeKernel(spreadKernel, spreadArgs, cu.getPaddedNumAtoms());
    tempPosq->copyTo(cu.getPosq());
    tempForces->copyTo(cu.getForce());
    double4 box = cu.getPeriodicBoxSize();
    return dispersionCoefficient/(box.x*box.y*box.z);
}

///* -------------------------------------------------------------------------- *
// *                           AmoebaWcaDispersion                              *
// * -------------------------------------------------------------------------- */
//
//static void computeAmoebaWcaDispersionForce( CudaContext& cu ) {
//
//    data.initializeGpu();
//    if( 0 && data.getLog() ){
//        (void) fprintf( data.getLog(), "Calling computeAmoebaWcaDispersionForce  " ); (void) fflush( data.getLog() );
//    }
//
//    kCalculateAmoebaWcaDispersionForces( data.getAmoebaGpu() );
//
//    if( 0 && data.getLog() ){
//        (void) fprintf( data.getLog(), " -- completed\n" ); (void) fflush( data.getLog() );
//    }
//}
//
//class CudaCalcAmoebaWcaDispersionForceKernel::ForceInfo : public CudaForceInfo {
//public:
//    ForceInfo(const AmoebaWcaDispersionForce& force) : force(force) {
//    }
//    bool areParticlesIdentical(int particle1, int particle2) {
//        double radius1, radius2, epsilon1, epsilon2;
//        force.getParticleParameters(particle1, radius1, epsilon1);
//        force.getParticleParameters(particle2, radius2, epsilon2);
//        return (radius1 == radius2 && epsilon1 == epsilon2);
//    }
//private:
//    const AmoebaWcaDispersionForce& force;
//};
//
//CudaCalcAmoebaWcaDispersionForceKernel::CudaCalcAmoebaWcaDispersionForceKernel(std::string name, const Platform& platform, CudaContext& cu, System& system) : 
//           CalcAmoebaWcaDispersionForceKernel(name, platform), cu(cu), system(system) {
//    data.incrementKernelCount();
//}
//
//CudaCalcAmoebaWcaDispersionForceKernel::~CudaCalcAmoebaWcaDispersionForceKernel() {
//    data.decrementKernelCount();
//}
//
//void CudaCalcAmoebaWcaDispersionForceKernel::initialize(const System& system, const AmoebaWcaDispersionForce& force) {
//
//    // per-particle parameters
//
//    int numParticles = system.getNumParticles();
//    std::vector<float> radii(numParticles);
//    std::vector<float> epsilons(numParticles);
//    for( int ii = 0; ii < numParticles; ii++ ){
//
//        double radius, epsilon;
//        force.getParticleParameters( ii, radius, epsilon );
//
//        radii[ii]         = static_cast<float>( radius );
//        epsilons[ii]      = static_cast<float>( epsilon );
//    }   
//    float totalMaximumDispersionEnergy =  static_cast<float>( AmoebaWcaDispersionForceImpl::getTotalMaximumDispersionEnergy( force ) );
//    gpuSetAmoebaWcaDispersionParameters( data.getAmoebaGpu(), radii, epsilons, totalMaximumDispersionEnergy,
//                                          static_cast<float>( force.getEpso( )),
//                                          static_cast<float>( force.getEpsh( )),
//                                          static_cast<float>( force.getRmino( )),
//                                          static_cast<float>( force.getRminh( )),
//                                          static_cast<float>( force.getAwater( )),
//                                          static_cast<float>( force.getShctd( )),
//                                          static_cast<float>( force.getDispoff( ) ) );
//    data.getAmoebaGpu()->gpuContext->forces.push_back(new ForceInfo(force));
//}
//
//double CudaCalcAmoebaWcaDispersionForceKernel::execute(ContextImpl& context, bool includeForces, bool includeEnergy) {
//    computeAmoebaWcaDispersionForce( data );
//    return 0.0;
//}
