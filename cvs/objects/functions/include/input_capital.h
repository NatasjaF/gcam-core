/*
* LEGAL NOTICE
* This computer software was prepared by Battelle Memorial Institute,
* hereinafter the Contractor, under Contract No. DE-AC05-76RL0 1830
* with the Department of Energy (DOE). NEITHER THE GOVERNMENT NOR THE
* CONTRACTOR MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY
* LIABILITY FOR THE USE OF THIS SOFTWARE. This notice including this
* sentence must appear on any copies of this computer software.
* 
* EXPORT CONTROL
* User agrees that the Software will not be shipped, transferred or
* exported into any country or used in any manner prohibited by the
* United States Export Administration Act or any other applicable
* export laws, restrictions or regulations (collectively the "Export Laws").
* Export of the Software may require some form of license or other
* authority from the U.S. Government, and failure to obtain such
* export control license may result in criminal liability under
* U.S. laws. In addition, if the Software is identified as export controlled
* items under the Export Laws, User represents and warrants that User
* is not a citizen, or otherwise located within, an embargoed nation
* (including without limitation Iran, Syria, Sudan, Cuba, and North Korea)
*     and that User is not otherwise prohibited
* under the Export Laws from receiving the Software.
* 
* Copyright 2011 Battelle Memorial Institute.  All Rights Reserved.
* Distributed as open-source under the terms of the Educational Community 
* License version 2.0 (ECL 2.0). http://www.opensource.org/licenses/ecl2.php
* 
* For further details, see: http://www.globalchange.umd.edu/models/gcam/
*
*/

#ifndef _INPUT_CAPITAL_H_
#define _INPUT_CAPITAL_H_
#if defined(_MSC_VER)
#pragma once
#endif

/*! 
 * \file input_capital.h
 * \ingroup Objects
 * \brief InputCapital class header file.
 * \author Josh Lurz
 */

#include <string>
#include <xercesc/dom/DOMNode.hpp>

#include "functions/include/minicam_input.h"
#include "util/base/include/value.h"
#include "util/base/include/time_vector.h"

class Tabs;

/*! 
 * \ingroup Objects
 * \brief Defines a single capital input to a MiniCAM production function.
 * \details A capital input represents a constant marginal cost input in the
 *          production function, such as capital or operating and maintenance.
 *          The input does not have an associated good in the model, and so the
 *          quantity is not tracked.
 *
 *          <b>XML specification for InputCapital</b>
 *          - XML name: \c input-capital
 *          - Contained by: Technology
 *          - Parsing inherited from class: MiniCAMInput
 *          - Attributes:
 *              - \c name MiniCAMInput::mName
 *          - Elements:
 *              - \c capital-overnight InputCapital::mCapitalOvernight
 *              - \c discount-rate InputCapital::mDiscountRate
 *              - \c input-cost InputCapital::mCost
 *              - \c tech-change InputCapital::mTechChange
 *
 * \author Josh Lurz
 */
class InputCapital: public MiniCAMInput
{
    friend class InputFactory;
public:

    static const std::string& getXMLNameStatic();

    const std::string& getXMLReportingName() const;    

    InputCapital* clone() const;

    virtual void XMLParse( const xercesc::DOMNode* aNode );

    virtual bool isSameType( const std::string& aType ) const;
    

    virtual void toInputXML( std::ostream& aOut,
                             Tabs* aTabs ) const;

    virtual void toDebugXML( const int aPeriod,
                             std::ostream& aOut,
                             Tabs* aTabs ) const;

    virtual void copyParam( const IInput* aInput,
                            const int aPeriod );

    virtual void copyParamsInto( InputCapital& aInput,
                                 const int aPeriod ) const;

    virtual void completeInit( const std::string& aRegionName,
                               const std::string& aSectorName,
                               const std::string& aSubsectorName,
                               const std::string& aTechName,
                               const IInfo* aTechInfo );
    
    virtual void initCalc( const std::string& aRegionName,
                           const std::string& aSectorName,
                           const bool aIsNewInvestmentPeriod,
                           const bool aIsTrade,
                           const IInfo* aTechInfo,
                           const int aPeriod );

    double getPhysicalDemand( const int aPeriod ) const;

    void setPhysicalDemand( const double aPhysicalDemand,
                            const std::string& aRegionName,
                            const int aPeriod );

    double getPrice( const std::string& aRegionName,
                     const int aPeriod ) const;
    
    virtual void setPrice( const std::string& aRegionName,
                           const double aPrice,
                           const int aPeriod );

    double getCO2EmissionsCoefficient( const std::string& aGHGName,
                                       const int aPeriod ) const;

    double getCoefficient( const int aPeriod ) const;

    void setCoefficient( const double aCoefficient,
                         const int aPeriod );

    void tabulateFixedQuantity( const std::string& aRegionName,
                                const double aFixedOutput,
                                const bool aIsInvestmentPeriod,
                                const int aPeriod );

    virtual void scaleCalibrationQuantity( const double aScaleFactor );

    virtual double getCalibrationQuantity( const int aPeriod ) const;

    bool hasTypeFlag( const int aTypeFlag ) const;

    virtual double getIncomeElasticity( const int aPeriod ) const;

    virtual double getPriceElasticity( const int aPeriod ) const;

    virtual double getTechChange( const int aPeriod ) const;
    
    virtual void doInterpolations( const int aYear, const int aPreviousYear,
                                   const int aNextYear, const IInput* aPreviousInput,
                                   const IInput* aNextInput );

protected:

    InputCapital();
    
    // Define data such that introspection utilities can process the data from this
    // subclass together with the data members of the parent classes.
    DEFINE_DATA_WITH_PARENT(
        MiniCAMInput,
        
        //! Cost of the non-energy input adjusted for the additional costs of the
        //! capture component.
        CREATE_ARRAY_VARIABLE( mAdjustedCosts, objects::PeriodVector<Value>, "adjusted-cost" ),
        
        //! Coefficient for production or demand function. Coefficients are not
        // read in and are initialized to 1, but can increase over time with
        // technical change.
        CREATE_ARRAY_VARIABLE( mAdjustedCoefficients, objects::PeriodVector<Value>, "adjusted-coef" ),
        
        //! Input specific technical change.
        CREATE_SIMPLE_VARIABLE( mTechChange, Value, "tech-change" ),
        
        //! Overnight cost of capital.
        CREATE_SIMPLE_VARIABLE( mCapitalOvernight, Value, "capital-overnight" ),
        
        //! Fixed charge rate for levelizing capital.
        // TODO: this should come from the capital market.
        CREATE_SIMPLE_VARIABLE( mFixedChargeRate, double, "fixed-charge-rate" ),
        
        //! Lifetime of capital that may be different from lifetime of technology.
        // This is a Value object so as to include units.
        CREATE_SIMPLE_VARIABLE( mLifetimeCapital, Value, "lifetime-capital" ),
        
        //! Calculated value for the levelized cost of capital.
        CREATE_SIMPLE_VARIABLE( mLevelizedCapitalCost, Value, "levelized-capital-cost" ),
        
        //! Technology capacity factor.
        // TODO: create one in technology and use that instead.
        CREATE_SIMPLE_VARIABLE( mCapacityFactor, double, "capacity-factor" )
    )

private:
    const static std::string XML_REPORTING_NAME; //!< tag name for reporting xml db 

    // Function to calculate levelized capital costs.
    double calcLevelizedCapitalCost( void ) const;

};

#endif // _INPUT_CAPITAL_H_
