/*
  Copyright (C) 2008-2013 by Andreas Lauser

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
/*!
 * \file
 *
 * \copydoc Ewoms::RichardsLensProblem
 */
#ifndef EWOMS_RICHARDS_LENS_PROBLEM_HH
#define EWOMS_RICHARDS_LENS_PROBLEM_HH

#include <ewoms/models/richards/richardsmodel.hh>

#include <opm/material/components/SimpleH2O.hpp>
#include <opm/material/fluidsystems/LiquidPhase.hpp>
#include <opm/material/fluidmatrixinteractions/RegularizedVanGenuchten.hpp>
#include <opm/material/fluidmatrixinteractions/LinearMaterial.hpp>
#include <opm/material/fluidmatrixinteractions/EffToAbsLaw.hpp>
#include <opm/material/fluidmatrixinteractions/MaterialTraits.hpp>

#include <dune/grid/io/file/dgfparser.hh>

#include <dune/common/version.hh>
#include <dune/common/fvector.hh>
#include <dune/common/fmatrix.hh>

namespace Ewoms {
template <class TypeTag>
class RichardsLensProblem;
}

namespace Opm {
namespace Properties {
NEW_TYPE_TAG(RichardsLensProblem, Richards);

// Use 2d YaspGrid
SET_TYPE_PROP(RichardsLensProblem, Grid, Dune::YaspGrid<2>);

// Set the physical problem to be solved
SET_TYPE_PROP(RichardsLensProblem, Problem, Ewoms::RichardsLensProblem<TypeTag>);

// Set the wetting phase
SET_PROP(RichardsLensProblem, WettingFluid)
{
private:
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;

public:
    typedef Opm::LiquidPhase<Scalar, Opm::SimpleH2O<Scalar> > type;
};

// Set the material Law
SET_PROP(RichardsLensProblem, MaterialLaw)
{
private:
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;
    enum { wettingPhaseIdx = FluidSystem::wettingPhaseIdx };
    enum { nonWettingPhaseIdx = FluidSystem::nonWettingPhaseIdx };

    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef Opm::TwoPhaseMaterialTraits<Scalar,
                                        /*wettingPhaseIdx=*/FluidSystem::wettingPhaseIdx,
                                        /*nonWettingPhaseIdx=*/FluidSystem::nonWettingPhaseIdx>
        Traits;

    // define the material law which is parameterized by effective
    // saturations
    typedef Opm::RegularizedVanGenuchten<Traits> EffectiveLaw;

public:
    // define the material law parameterized by absolute saturations
    typedef Opm::EffToAbsLaw<EffectiveLaw> type;
};

// Enable gravitational acceleration
SET_BOOL_PROP(RichardsLensProblem, EnableGravity, true);

// Enable partial reassembly of the linearization
SET_BOOL_PROP(RichardsLensProblem, EnablePartialRelinearization, true);

// Enable re-use of the linearization of the last iteration of the
// previous for the first iteration of the current time step?
SET_BOOL_PROP(RichardsLensProblem, EnableLinearizationRecycling, true);

// Use central differences to approximate the Jacobian matrix
SET_INT_PROP(RichardsLensProblem, NumericDifferenceMethod, 0);

// Set the maximum number of newton iterations of a time step
SET_INT_PROP(RichardsLensProblem, NewtonMaxIterations, 28);

// Set the "desireable" number of newton iterations of a time step
SET_INT_PROP(RichardsLensProblem, NewtonTargetIterations, 18);

// Do not write the intermediate results of the newton method
SET_BOOL_PROP(RichardsLensProblem, NewtonWriteConvergence, false);

// The default for the end time of the simulation
SET_SCALAR_PROP(RichardsLensProblem, EndTime, 3000);

// The default for the initial time step size of the simulation
SET_SCALAR_PROP(RichardsLensProblem, InitialTimeStepSize, 100);

// The default DGF file to load
SET_STRING_PROP(RichardsLensProblem, GridFile, "./data/richardslens_24x16.dgf");
}} // namespace Opm, Properties

namespace Ewoms {
/*!
 * \ingroup TestProblems
 *
 * \brief A water infiltration problem with a low-permeability lens
 *        embedded into a high-permeability domain.
 *
 * The domain is rectangular. The left and right boundaries are
 * free-flow boundaries with fixed water pressure which corrosponds to
 * a fixed saturation of \f$S_w = 0\f$ in the Richards model, the
 * bottom boundary is closed. The top boundary is also closed except
 * for an infiltration section, where water is infiltrating into an
 * initially unsaturated porous medium. This problem is very similar
 * the the \c LensProblem, with the main difference being that the domain
 * is initally fully saturated by gas instead of water and water
 * instead of a \c DNAPL infiltrates from the top.
 */
template <class TypeTag>
class RichardsLensProblem : public GET_PROP_TYPE(TypeTag, BaseProblem)
{
    typedef typename GET_PROP_TYPE(TypeTag, BaseProblem) ParentType;

    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, RateVector) RateVector;
    typedef typename GET_PROP_TYPE(TypeTag, BoundaryRateVector) BoundaryRateVector;
    typedef typename GET_PROP_TYPE(TypeTag, PrimaryVariables) PrimaryVariables;
    typedef typename GET_PROP_TYPE(TypeTag, Simulator) Simulator;
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, Model) Model;

    typedef typename GET_PROP_TYPE(TypeTag, Indices) Indices;
    enum {
        // copy some indices for convenience
        pressureWIdx = Indices::pressureWIdx,
        contiEqIdx = Indices::contiEqIdx,
        wettingPhaseIdx = FluidSystem::wettingPhaseIdx,
        nonWettingPhaseIdx = FluidSystem::nonWettingPhaseIdx,
        numPhases = FluidSystem::numPhases,

        // Grid and world dimension
        dimWorld = GridView::dimensionworld
    };

    // get the material law from the property system
    typedef typename GET_PROP_TYPE(TypeTag, MaterialLaw) MaterialLaw;
    //! The parameters of the material law to be used
    typedef typename MaterialLaw::Params MaterialLawParams;

    typedef typename GridView::ctype CoordScalar;
    typedef Dune::FieldVector<CoordScalar, dimWorld> GlobalPosition;
    typedef Dune::FieldVector<Scalar, numPhases> PhaseVector;
    typedef Dune::FieldMatrix<Scalar, dimWorld, dimWorld> DimMatrix;

public:
    /*!
     * \copydoc Doxygen::defaultProblemConstructor
     */
    RichardsLensProblem(Simulator &simulator)
        : ParentType(simulator)
        , pnRef_(1e5)
    {
        eps_ = 3e-6;
        pnRef_ = 1e5;

        lensLowerLeft_[0] = 1.0;
        lensLowerLeft_[1] = 2.0;

        lensUpperRight_[0] = 4.0;
        lensUpperRight_[1] = 3.0;

        // parameters for the Van Genuchten law
        // alpha and n
        lensMaterialParams_.setVgAlpha(0.00045);
        lensMaterialParams_.setVgN(7.3);
        lensMaterialParams_.finalize();

        outerMaterialParams_.setVgAlpha(0.0037);
        outerMaterialParams_.setVgN(4.7);
        outerMaterialParams_.finalize();

        // parameters for the linear law
        // minimum and maximum pressures
        //        lensMaterialParams_.setEntryPC(0);
        //        outerMaterialParams_.setEntryPC(0);
        //        lensMaterialParams_.setMaxPC(0);
        //        outerMaterialParams_.setMaxPC(0);

        lensK_ = this->toDimMatrix_(1e-12);
        outerK_ = this->toDimMatrix_(5e-12);
    }

    /*!
     * \name Problem parameters
     */
    //! \{

    /*!
     * \copydoc FvBaseProblem::name
     */
    std::string name() const
    { return "lens_richards"; }

    /*!
     * \copydoc FvBaseMultiPhaseProblem::temperature
     */
    template <class Context>
    Scalar temperature(const Context &context, int spaceIdx, int timeIdx) const
    { return 273.15 + 10; } // -> 10°C

    /*!
     * \copydoc FvBaseMultiPhaseProblem::intrinsicPermeability
     */
    template <class Context>
    const DimMatrix &intrinsicPermeability(const Context &context,
                                           int spaceIdx,
                                           int timeIdx) const
    {
        const GlobalPosition &pos = context.pos(spaceIdx, timeIdx);
        if (isInLens_(pos))
            return lensK_;
        return outerK_;
    }

    /*!
     * \copydoc FvBaseMultiPhaseProblem::porosity
     */
    template <class Context>
    Scalar porosity(const Context &context, int spaceIdx, int timeIdx) const
    { return 0.4; }

    /*!
     * \copydoc FvBaseMultiPhaseProblem::materialLawParams
     */
    template <class Context>
    const MaterialLawParams &materialLawParams(const Context &context,
                                               int spaceIdx,
                                               int timeIdx) const
    {
        const auto &pos = context.pos(spaceIdx, timeIdx);
        if (isInLens_(pos))
            return lensMaterialParams_;
        return outerMaterialParams_;
    }

    /*!
     * \brief Return the reference pressure [Pa] of the wetting phase.
     *
     * \copydetails Doxygen::contextParams
     */
    template <class Context>
    Scalar referencePressure(const Context &context,
                             int spaceIdx,
                             int timeIdx) const
    { return pnRef_; }

    //! \}

    /*!
     * \name Boundary conditions
     */
    //! \{

    /*!
     * \copydoc FvBaseProblem::boundary
     */
    template <class Context>
    void boundary(BoundaryRateVector &values,
                  const Context &context,
                  int spaceIdx,
                  int timeIdx) const
    {
        const auto &pos = context.pos(spaceIdx, timeIdx);

        if (onLeftBoundary_(pos) || onRightBoundary_(pos)) {
            const auto &materialParams = this->materialLawParams(context, spaceIdx, timeIdx);

            Scalar Sw = 0.0;
            Opm::ImmiscibleFluidState<Scalar, FluidSystem> fs;
            fs.setSaturation(wettingPhaseIdx, Sw);
            fs.setSaturation(nonWettingPhaseIdx, 1.0 - Sw);

            PhaseVector pC;
            MaterialLaw::capillaryPressures(pC, materialParams, fs);
            fs.setPressure(wettingPhaseIdx, pnRef_ + pC[wettingPhaseIdx] - pC[nonWettingPhaseIdx]);
            fs.setPressure(nonWettingPhaseIdx, pnRef_);

            values.setFreeFlow(context, spaceIdx, timeIdx, fs);
        }
        else if (onInlet_(pos)) {
            RateVector massRate(0.0);

            // inflow of water
            massRate[contiEqIdx] = -0.04; // kg / (m * s)

            values.setMassRate(massRate);
        }
        else
            values.setNoFlow();
    }

    //! \}

    /*!
     * \name Volumetric terms
     */
    //! \{

    /*!
     * \copydoc FvBaseProblem::initial
     */
    template <class Context>
    void initial(PrimaryVariables &values,
                 const Context &context,
                 int spaceIdx,
                 int timeIdx) const
    {
        const auto &materialParams = this->materialLawParams(context, spaceIdx, timeIdx);

        Scalar Sw = 0.0;
        Opm::ImmiscibleFluidState<Scalar, FluidSystem> fs;
        fs.setSaturation(wettingPhaseIdx, Sw);
        fs.setSaturation(nonWettingPhaseIdx, 1.0 - Sw);

        PhaseVector pC;
        MaterialLaw::capillaryPressures(pC, materialParams, fs);
        values[pressureWIdx] = pnRef_ + (pC[wettingPhaseIdx] - pC[nonWettingPhaseIdx]);
    }

    /*!
     * \copydoc FvBaseProblem::source
     *
     * For this problem, the source term of all components is 0
     * everywhere.
     */
    template <class Context>
    void source(RateVector &rate,
                const Context &context,
                int spaceIdx,
                int timeIdx) const
    { rate = Scalar(0.0); }

    //! \}

private:
    bool onLeftBoundary_(const GlobalPosition &pos) const
    { return pos[0] < this->boundingBoxMin()[0] + eps_; }

    bool onRightBoundary_(const GlobalPosition &pos) const
    { return pos[0] > this->boundingBoxMax()[0] - eps_; }

    bool onLowerBoundary_(const GlobalPosition &pos) const
    { return pos[1] < this->boundingBoxMin()[1] + eps_; }

    bool onUpperBoundary_(const GlobalPosition &pos) const
    { return pos[1] > this->boundingBoxMax()[1] - eps_; }

    bool onInlet_(const GlobalPosition &pos) const
    {
        Scalar width = this->boundingBoxMax()[0] - this->boundingBoxMin()[0];
        Scalar lambda = (this->boundingBoxMax()[0] - pos[0]) / width;
        return onUpperBoundary_(pos) && 0.5 < lambda && lambda < 2.0 / 3.0;
    }

    bool isInLens_(const GlobalPosition &pos) const
    {
        for (int i = 0; i < dimWorld; ++i) {
            if (pos[i] < lensLowerLeft_[i] || pos[i] > lensUpperRight_[i])
                return false;
        }
        return true;
    }

    GlobalPosition lensLowerLeft_;
    GlobalPosition lensUpperRight_;

    DimMatrix lensK_;
    DimMatrix outerK_;
    MaterialLawParams lensMaterialParams_;
    MaterialLawParams outerMaterialParams_;

    Scalar eps_;
    Scalar pnRef_;
};
} // namespace Ewoms

#endif
