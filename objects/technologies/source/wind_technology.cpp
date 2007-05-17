/*!
 * wind_technology.cpp
 * Created: 03/20/2007
 * Version: 04/16/2007
 *
 * This software, which is provided in confidence, was prepared by employees
 * of Pacific Northwest National Laboratory operated by Battelle Memorial
 * Institute. Battelle has certain unperfected rights in the software
 * which should not be copied or otherwise disseminated outside your
 * organization without the express written authorization from Battelle.
 * All rights to the software are reserved by Battelle.   Battelle makes no
 * warranty, express or implied, and assumes no liability or responsibility
 * for the use of this software.
 */

// include files ***********************************************************

#include "util/base/include/definitions.h"
#include "technologies/include/wind_technology.h"
#include "technologies/include/marginal_profit_calculator.h"
#include "technologies/include/iproduction_state.h"
#include "marketplace/include/marketplace.h"
#include "containers/include/iinfo.h"
#include "util/base/include/TValidatorInfo.h"
#include "util/base/include/util.h"
#include "util/base/include/xml_helper.h"

#include <sstream>
#include <algorithm>
#include <cmath>

#include <boost/math/special_functions/erf.hpp>
using boost::math::erf;

// namespaces **************************************************************

// WindTechnology::kWhrtoGJ ************************************************

const double WindTechnology::kWhrtoGJ = 0.0036;

// WindTechnology::sXMLTagNames ********************************************

std::string WindTechnology::sXMLTagNames[] =
{
   std::string( "air-density" ),
   std::string( "average-wind-speed" ),
   std::string( "capital-cost" ),
   std::string( "cutout-speed" ),
   std::string( "fcr" ),
   std::string( "grid-connection-cost" ),
   std::string( "om" ),
   std::string( "reference-height" ),
   std::string( "rotor-diameter" ),
   std::string( "turbine-density" ),
   std::string( "turbine-derating" ),
   std::string( "turbine-hub-height" ),
   std::string( "turbine-rating" ),
   std::string( "wind-farm-loss" ),
   std::string( "wind-velocity-exponent" )
};

// Constructor: WindTechnology *********************************************

/*! Default constructor
 *  \param aName the name of the technology
 *  \param aYear the year
 */
WindTechnology::WindTechnology(
   const std::string& aName,
   const int          aYear )
   : parent( aName, aYear ),
     mCapitalCost( 261.83 ),
     mConnectCost( -1 ),
     mCutOutSpeed( -1 ),
     mFCR( 0.0856 ),
     mGenerationCost( -1 ),
     mGridConnectionCost( 392.75 ),
     mOM( 2.62 ),
     mRealizedTurbineOutput( -1 ),
     mRotorDiameter( -1 ),
     mTurbineDensity( -1 ),
     mTurbineDerating( -1 ),
     mTurbineHubHeight( -1 ),
     mTurbineRating( -1 ),
     mWindCapacityFactor( -1 ),
     mWindFarmLoss( -1 )
{
}

/*! Copy constructor
 *  \param other the instance to copy
 */
WindTechnology::WindTechnology(const WindTechnology& other)
   : parent( other )
{
}

// Destructor: WindTechnology **********************************************

WindTechnology::~WindTechnology(void)
{
}

// WindTechnology::operator = **********************************************

/*! Assignment operator
 *  \param other the instance to copy
 *  \return *this
 */
WindTechnology& WindTechnology::operator = (const WindTechnology& other)
{
  if ( &other != this )
  {
     parent::operator = ( other );
  }

  return *this;
}

// WindTechnology::calcCost ************************************************

// Documentation is inherited
void WindTechnology::calcCost(
   const std::string& aRegionName,
   const std::string& aSectorName,
   const int          aPeriod )
{
   // Get marketplace and calculate costs
   Marketplace*       pMarketplace = scenario->getMarketplace();
   const std::string& fuelName     = mTechData->getFuelName();
   const IInfo*       pInfo        = pMarketplace->getMarketInfo( fuelName, aRegionName, aPeriod, true );

   // Equation 4:
   mRealizedTurbineOutput = calcRealizedTurbineOutput( pInfo );

   // Equation 3:
   // WindCapacityFactor = RealizedTurbineOutput / TurbineRating
   mWindCapacityFactor = mRealizedTurbineOutput / mTurbineRating;

   // Equation 6:
   // CConnect = FCR * DConnect * ( gridConnectionCost / ( WindCapacityFactor * 1000.0 * kWhrtoGJ * 24 * 365 * 1 ) )
   double dConnect = pMarketplace->getPrice( fuelName, aRegionName, aPeriod );
   mConnectCost    = mFCR * dConnect * ( mGridConnectionCost / ( mWindCapacityFactor * 1000.0 * kWhrtoGJ * 24.0 * 365.0 ) );

   // Equation 2:
   // CGeneration = ( FCR * CapitalCost + OM ) / ( 24 * 365 * WindCapacityFactor * kWhrtoGJ )
   mGenerationCost = ( mFCR * mCapitalCost + mOM ) / ( 24.0 * 365.0 * mWindCapacityFactor * kWhrtoGJ );

   // Equation 1:
   // CTotal = CGeneration + CConnect
   double cTotal = mGenerationCost + mConnectCost;

   // Save to array
   mCosts[ aPeriod ] = std::max( cTotal, util::getSmallNumber() );
}

// WindTechnology::calcIdealTurbineOutput **********************************

/*! 
 *  \param aAveWindSpeed the average Wind Speed
 *  \param aDiameter the turbine blade diameter (in meters)
 *  \param aAirDensity the average Air density (in g/m^3)
 */
double WindTechnology::calcIdealTurbineOutput(
   double aAveWindSpeed,
    double aDiameter,
    double aAirDensity )
{
   /*
    * The ideal maximum extractable power, known as the Betz limit, has a
    * coefficient of Cp = 16/27. At this limit, the integral evaluates to
    * (Equation A-6):
    *   Prb = p * ( 2 / 3 * D )^2 * V^3
    * where:
    *   p = the average Air density (in g/m^3)
    *   D = the turbine blade diameter (in meters)
    *   V = the average Wind Speed
    */
   return aAirDensity * std::pow( 2.0 / 3.0 * aDiameter, 2.0 ) * std::pow( aAveWindSpeed, 3.0 );
}

// WindTechnology::calcRealizedTurbineOutput *******************************
/*! Compute the realized turbine output
 *  \param apInfo pointer to the market info
 */
double WindTechnology::calcRealizedTurbineOutput( const IInfo* apInfo ) const
{
   // Equation 5:
   // aveWindSpeedAtHub = aveWindSpeed * ( turbineHubHeight / referenceHeight ) ^ windVelocityExponent
   double aveWindSpeed = apInfo->getDouble( sXMLTagNames[ AVERAGE_WIND_SPEED_KEY ], true );
   double referenceHeight = apInfo->getDouble( sXMLTagNames[ REFERENCE_HEIGHT_KEY ], true );
   double windVelocityExponent = apInfo->getDouble( sXMLTagNames[ WIND_VELOCITY_EXPONENT_KEY ], true );
   double aveWindSpeedAtHub = aveWindSpeed * std::pow( mTurbineHubHeight / referenceHeight, windVelocityExponent );

   // Equation 4:
   // RealizedTurbineOutput = ( IdealTurbineOutput / 10^6 ) * TurbineCoefficient * ( 1 - Derating ) * ( 1 - WindFarmLoss )
   double airDensity = apInfo->getDouble( sXMLTagNames[ AIR_DENSITY_KEY ], true );
   double realizedTurbineOutput = ( calcIdealTurbineOutput( aveWindSpeedAtHub, mRotorDiameter, airDensity ) / 1.0e6 ) * calcTurbineCoefficient( aveWindSpeedAtHub, mTurbineRating, mRotorDiameter, airDensity, mCutOutSpeed ) * ( 1.0 - mTurbineDerating ) * ( 1.0 - mWindFarmLoss );
   mWindPowerVariance = computeWindPowerVariance( aveWindSpeedAtHub, mTurbineRating, mRotorDiameter, airDensity, mCutOutSpeed );
   return realizedTurbineOutput;
}

// WindTechnology::calcResourceArea ****************************************

/*! Calculate the resource area in km^2
 *  \param aRegionName the region name
 *  \param aSectorName the sector name
 *  \param aVariableDemand the variable demand
 *  \param aPeriod the period
 *  \return the resource area
 */
double WindTechnology::calcResourceArea(
   const std::string& aRegionName,
   const std::string& aSectorName,
   double             aVariableDemand,
   const int          aPeriod )
{
   // Get marketplace and calculate costs
   Marketplace*       pMarketplace = scenario->getMarketplace();
   const std::string& fuelName     = mTechData->getFuelName();
   const IInfo*       pInfo        = pMarketplace->getMarketInfo( fuelName, aRegionName, aPeriod, true );

   // Equation 4:
   mRealizedTurbineOutput = calcRealizedTurbineOutput( pInfo );

   // Equation 3:
   // WindCapacityFactor = RealizedTurbineOutput / TurbineRating
   mWindCapacityFactor = mRealizedTurbineOutput / mTurbineRating;

   // Equation 7:
   // WindGeneration / ( kWhrtoGJ * 10^-9 ) = TurbineDensity * ResourceArea * WindCapacityFactor * 1000 * 24 * 365
   // ResourceArea = ( WindGeneration / ( kWhrtoGJ * 10^-9 ) ) / ( TurbineDensity * WindCapacityFactor * 1000 * 24 * 365 )
   double windGeneration = aVariableDemand;
   double resourceArea = ( windGeneration / ( kWhrtoGJ * 1.0e-9 ) ) / ( mTurbineDensity * mWindCapacityFactor * 1000.0 * 24.0 * 365.0 );

   return resourceArea;
}

// WindTechnology::calcShare ***********************************************

// Documentation is inherited
double WindTechnology::calcShare(
   const std::string& aRegionName,
   const std::string& aSectorName, 
   const GDP*         aGDP,
   const int          aPeriod ) const
{
   return parent::calcShare( aRegionName, aSectorName, aGDP, aPeriod );
}

// WindTechnology::calcTurbineCoefficient **********************************

/*! Compute the capture coefficient for a turbine with a finite power rating.
 *  \param aAveWindSpeed the average Wind Speed
 *  \param aRating the turbine Rating (in MW)
 *  \param aDiameter the turbine Blade Diameter (in meters)
 *  \param aAirDensity the average Air density (in g/m^3)
 *  \param aCutoutSpeed the cut-out Speed (m/s)
 */
double WindTechnology::calcTurbineCoefficient(
   double aAveWindSpeed,
    double aRating,
    double aDiameter,
    double aAirDensity,
    double aCutoutSpeed )
{
   static const double PI     = 2.0 * std::asin( 1.0 );
   static const double sqrtPI = std::sqrt( PI );

   /*
    * Equation A-8
    * Calculate the final cutout speed (Xf)
    *   Xs = ( sqrt( pi ) / 2) * ( Vs / Vave )
    */
   double Xf = ( sqrtPI / 2.0 ) * ( aCutoutSpeed / aAveWindSpeed );

   /*
    * Equation A-10
    * Calculate the velocity (solve for v)
    * 1/8pD^2(16/27)v^3 = Prated
    * v = cube-root( Prated / ( 1/8pD^2(16/27) )
    */
   double aRatingInWatts = aRating * 1.0e6;
   double velocity = std::pow( aRatingInWatts / ( 1.0 / 8.0 * aAirDensity * PI * std::pow( aDiameter, 2.0 ) * 16.0 / 27.0 ), 1.0 / 3.0 );

   /*
    * Equation A-11
    * Calculate the finite power rating (Xr)
    * Xr = ( sqrt( pi ) / 2 ) * V / Vave
    */
   double Xr = ( sqrtPI / 2.0 ) * velocity / aAveWindSpeed;

   /*
    * Equation A-7:
    * Compute the power up to a finite cutout for Xr
    * speed is ():
    *
    *   CCfinite_cutout( Xs ) =
    *      Erf( Xs ) - ( 4 / ( 3 * sqrt( pi ) ) ) * Xs * ( Xs^2 + 3 / 2 ) * e^-Xs^2
    */
   double CCfiniteCutout = erf( Xr ) - ( 4.0 / ( 3.0 * sqrtPI ) ) * Xr * ( std::pow( Xr, 2.0 ) + 3.0 / 2.0 ) * std::exp( -std::pow( Xr, 2.0 ) );

   /*
    * Equation A-9:
    * Finally, compute the capture coefficient
    * CCfinite_power( Xr, Xf ) = 
    *    CCfinite_cutout( Xr ) + ( Xr^3 / ( 3 / 4 * sqrt( pi ) ) ) * ( e ^-Xr^2 - e^-Xf^2 )
    */
   double CCfinitePower = CCfiniteCutout + ( std::pow( Xr, 3.0 ) / ( 3.0 / 4.0 * sqrtPI ) ) * ( std::exp( -std::pow( Xr, 2.0 ) ) - std::exp( -std::pow( Xf, 2.0 ) ) );

   return CCfinitePower;
}

// WindTechnology::clone ***************************************************

// Documentation is inherited
WindTechnology* WindTechnology::clone( void ) const
{
  return new WindTechnology( *this );
}

// WindTechnology::completeInit ********************************************

// Documentation is inherited
void WindTechnology::completeInit(
   const std::string&              aRegionName,
   const std::string&              aSectorName,
   DependencyFinder*               aDepFinder,
   const IInfo*                    aSubsectorIInfo,
   ILandAllocator*                 aLandAllocator,
   const GlobalTechnologyDatabase* aGlobalTechDB )
{
   parent::completeInit( aRegionName, aSectorName, aDepFinder, aSubsectorIInfo, aLandAllocator, aGlobalTechDB );

   // Validate input parameters
   typedef ObjECTS::TValidatorInfo<> validator_type;
   validator_type   validator[] =
   {
      validator_type( mCapitalCost, sXMLTagNames[ CAPITAL_COST_KEY ], mCapitalCost > 0 ),
      validator_type( mCutOutSpeed, sXMLTagNames[ CUTOUT_SPEED_KEY ], mCutOutSpeed > 0 ),
      validator_type( mFCR, sXMLTagNames[ FCR_KEY ], mFCR > 0 ),
      validator_type( mRotorDiameter, sXMLTagNames[ ROTOR_DIAMETER_KEY ], mRotorDiameter > 0 ),
      validator_type( mTurbineDensity, sXMLTagNames[ TURBINE_DENSITY_KEY ], mTurbineDensity > 0 ),
      validator_type( mTurbineDerating, sXMLTagNames[ TURBINE_DERATING_KEY ], mTurbineDerating > 0 ),
      validator_type( mTurbineHubHeight, sXMLTagNames[ TURBINE_HUB_HEIGHT_KEY ], mTurbineHubHeight > 0 ),
      validator_type( mTurbineRating, sXMLTagNames[ TURBINE_RATING_KEY ], mTurbineRating > 0 ),
      validator_type( mWindFarmLoss, sXMLTagNames[ WIND_FARM_LOSS_KEY ], mWindFarmLoss > 0 )
   };

   unsigned short numParams = sizeof( validator ) / sizeof( validator[0] );
   std::string    msg       = ObjECTS::getInvalidNames(
      &validator[0],
      &validator[numParams] );

   if ( msg.length() )
   // Invalid input parameter
   {
      ILogger& mainLog = ILogger::getLogger( "main_log" );
      mainLog.setLevel( ILogger::ERROR );
      mainLog << "Invalid input parameter(s) to "
         << getXMLNameStatic1D()
         << " in sector " << aSectorName
         << ": " << msg << std::endl;
      exit( -1 );
   }
}

// WindTechnology::getEfficiency *******************************************

// Documentation is inherited
double WindTechnology::getEfficiency( const int aPeriod ) const
{
   return parent::getEfficiency( aPeriod );
}

// WindTechnology::getFuelCost *********************************************

// Documentation is inherited
double WindTechnology::getFuelCost(
   const std::string& aRegionName,
   const std::string& aSectorName,
   const int          aPeriod ) const
{
   return parent::getFuelCost( aRegionName, aSectorName, aPeriod );
}

// WindTechnology::getNonEnergyCost ****************************************

// Documentation is inherited
double WindTechnology::getNonEnergyCost( const int aPeriod ) const
{
   return parent::getNonEnergyCost( aPeriod );
}

// WindTechnology::getXMLName1D ********************************************

// Documentation is inherited
const std::string& WindTechnology::getXMLName1D( void ) const
{
   return getXMLNameStatic1D();
}

// WindTechnology::getXMLNameStatic1D **************************************

// Documentation is inherited
const std::string& WindTechnology::getXMLNameStatic1D( void )
{
   static const std::string XML_NAME1D = "wind-technology";

   return XML_NAME1D;
}

// WindTechnology::initCalc ************************************************

// Documentation is inherited
void WindTechnology::initCalc(
   const std::string& aRegionName,
   const std::string& aSectorName,
   const IInfo*       aSubsectorIInfo,
   const Demographic* aDemographics,
   const int          aPeriod )
{
   parent::initCalc( aRegionName, aSectorName, aSubsectorIInfo, aDemographics, aPeriod );

   // Get marketplace and make sure we have the total annual irradiance
   Marketplace*       pMarketplace = scenario->getMarketplace();
   const std::string& fuelName     = mTechData->getFuelName();
   const IInfo*       pInfo        = pMarketplace->getMarketInfo( fuelName, aRegionName, aPeriod, true );
   std::string        msg          = "";

   if ( !pInfo )
   {
      std::ostringstream ostr;
      ostr << "Error getting marketplace info for ( "
           << fuelName << ", "
           << aRegionName << ", "
           << aPeriod << " )"
           << std::ends;
      msg = ostr.str();
   }
   else
   {
      typedef ObjECTS::TValidatorInfo<> validator_type;
      double           notUsed;
      validator_type   validator[] =
      {
         validator_type( notUsed, sXMLTagNames[ AVERAGE_WIND_SPEED_KEY ], pInfo->hasValue( sXMLTagNames[ AVERAGE_WIND_SPEED_KEY ] ) ),
         validator_type( notUsed, sXMLTagNames[ AIR_DENSITY_KEY ], pInfo->hasValue( sXMLTagNames[ AIR_DENSITY_KEY ] ) ),
         validator_type( notUsed, sXMLTagNames[ REFERENCE_HEIGHT_KEY ], pInfo->hasValue( sXMLTagNames[ REFERENCE_HEIGHT_KEY ] ) ),
         validator_type( notUsed, sXMLTagNames[ WIND_VELOCITY_EXPONENT_KEY ], pInfo->hasValue( sXMLTagNames[ WIND_VELOCITY_EXPONENT_KEY ] ) )
      };

      unsigned short numParams = sizeof( validator ) / sizeof( validator[0] );
      msg = ObjECTS::getInvalidNames(
         &validator[0],
         &validator[numParams] );
      if ( msg.length() )
      {
         std::ostringstream ostr;
         ostr << "Invalid input parameter(s) to "
              << getXMLNameStatic1D()
              << " in sector " << aSectorName
              << ": " << msg << std::ends;
         msg = ostr.str();
      }
   }

   if ( msg.length() )
      // Invalid input parameter
   {
      ILogger& mainLog = ILogger::getLogger( "main_log" );
      mainLog.setLevel( ILogger::ERROR );
      mainLog << msg << std::endl;
   }
}

// WindTechnology::postCalc ************************************************

// Documentation is inherited
void WindTechnology::postCalc(
   const std::string& aRegionName,
   const int          aPeriod )
{
   parent::postCalc( aRegionName, aPeriod );
}

// WindTechnology::production **********************************************

// Documentation is inherited
void WindTechnology::production(
   const std::string& aRegionName,
   const std::string& aSectorName, 
   double             aVariableDemand,
   double             aFixedOutputScaleFactor,
   const GDP*         aGDP,
   const int          aPeriod )
{
   // Can't have a scale factor and positive demand.
   assert( aFixedOutputScaleFactor == 1 || aVariableDemand == 0 );

   // Can't have negative variable demand.
   assert( aVariableDemand >= 0 && util::isValidNumber( aVariableDemand ) );

   // Check for positive variable demand and positive fixed output.
   assert( mFixedOutput == IProductionState::fixedOutputDefault() || util::isEqual( aVariableDemand, 0.0 ) );

   // Check that a state has been created for the period.
   assert( mProductionState[ aPeriod ] );

   // Construct a marginal profit calculator. This allows the calculation of
   // marginal profits to be lazy.
   MarginalProfitCalculator marginalProfitCalc( this );

   // Use the production state to determine output.
   double primaryOutput = mProductionState[ aPeriod ]->calcProduction( aRegionName, aSectorName, aVariableDemand, &marginalProfitCalc, aFixedOutputScaleFactor, mShutdownDeciders, aPeriod );

   // Calculate input demand.
   mInput[ aPeriod ] = calcResourceArea( aRegionName, aSectorName, aVariableDemand, aPeriod );

   // set demand for fuel in marketplace
   Marketplace*       marketplace = scenario->getMarketplace();
   const std::string& fuelName    = mTechData->getFuelName();
   if( ( fuelName != "renewable" ) &&
       ( fuelName != "none" ) &&
       mInput[ aPeriod ] > util::getSmallNumber() )
   {
      marketplace->addToDemand( fuelName, aRegionName, mInput[ aPeriod ], aPeriod );
   }

   // Set the supply of the good to the marketplace.
   calcEmissionsAndOutputs( aRegionName, mInput[ aPeriod ], primaryOutput, aGDP, aPeriod );
}

// WindTechnology::toDebugXMLDerived ***************************************

// Documentation is inherited
void WindTechnology::toDebugXMLDerived(
   const int     period,
   std::ostream& out,
   Tabs*         tabs ) const
{
   XMLWriteElement( mCapitalCost, sXMLTagNames[ CAPITAL_COST_KEY ], out, tabs );
   XMLWriteElement( mConnectCost, "connection-cost", out, tabs );
   XMLWriteElement( mCutOutSpeed, sXMLTagNames[ CUTOUT_SPEED_KEY ], out, tabs );
   XMLWriteElement( mFCR, sXMLTagNames[ FCR_KEY ], out, tabs );
   XMLWriteElement( mGenerationCost, "generation-cost", out, tabs );
   XMLWriteElement( mGridConnectionCost, sXMLTagNames[ GRID_CONNECTION_COST_KEY ], out, tabs );
   XMLWriteElement( mOM, sXMLTagNames[ OM_KEY ], out, tabs );
   XMLWriteElement( mRealizedTurbineOutput, "realized-turbine-output", out, tabs );
   XMLWriteElement( mRotorDiameter, sXMLTagNames[ ROTOR_DIAMETER_KEY ], out, tabs );
   XMLWriteElement( mTurbineDensity, sXMLTagNames[ TURBINE_DENSITY_KEY ], out, tabs );
   XMLWriteElement( mTurbineDerating, sXMLTagNames[ TURBINE_DERATING_KEY ], out, tabs );
   XMLWriteElement( mTurbineHubHeight, sXMLTagNames[ TURBINE_HUB_HEIGHT_KEY ], out, tabs );
   XMLWriteElement( mTurbineRating, sXMLTagNames[ TURBINE_RATING_KEY ], out, tabs );
   XMLWriteElement( mWindCapacityFactor, "wind-capacity-factor", out, tabs );
   XMLWriteElement( mWindFarmLoss, sXMLTagNames[ WIND_FARM_LOSS_KEY ], out, tabs );
   XMLWriteElement( mWindPowerVariance, "wind-power-variance", out, tabs );
}

// WindTechnology::toInputXMLDerived ***************************************

// Documentation is inherited
void WindTechnology::toInputXMLDerived(
   std::ostream& out,
   Tabs*         tabs ) const
{
   XMLWriteElementCheckDefault( mCapitalCost, sXMLTagNames[ CAPITAL_COST_KEY ], out, tabs, double( 1000.0 ) );
   XMLWriteElement( mCutOutSpeed, sXMLTagNames[ CUTOUT_SPEED_KEY ], out, tabs );
   XMLWriteElementCheckDefault( mFCR, sXMLTagNames[ FCR_KEY ], out, tabs, double( 0.0856 ) );
   XMLWriteElementCheckDefault( mGridConnectionCost, sXMLTagNames[ GRID_CONNECTION_COST_KEY ], out, tabs, double( 1500.0 ) );
   XMLWriteElementCheckDefault( mOM, sXMLTagNames[ OM_KEY ], out, tabs, double( 10.0 ) );
   XMLWriteElement( mRotorDiameter, sXMLTagNames[ ROTOR_DIAMETER_KEY ], out, tabs );
   XMLWriteElement( mTurbineDensity, sXMLTagNames[ TURBINE_DENSITY_KEY ], out, tabs );
   XMLWriteElement( mTurbineDerating, sXMLTagNames[ TURBINE_DERATING_KEY ], out, tabs );
   XMLWriteElement( mTurbineHubHeight, sXMLTagNames[ TURBINE_HUB_HEIGHT_KEY ], out, tabs );
   XMLWriteElement( mTurbineRating, sXMLTagNames[ TURBINE_RATING_KEY ], out, tabs );
   XMLWriteElement( mWindFarmLoss, sXMLTagNames[ WIND_FARM_LOSS_KEY ], out, tabs );
}

// WindTechnology::XMLDerivedClassParse ************************************

// Documentation is inherited
bool WindTechnology::XMLDerivedClassParse(
   const std::string&      nodeName,
   const xercesc::DOMNode* curr )
{
   if ( nodeName == sXMLTagNames[ CAPITAL_COST_KEY ] )
   {
      mCapitalCost = XMLHelper<double>::getValue( curr );
   }
   else if ( nodeName == sXMLTagNames[ CUTOUT_SPEED_KEY ] )
   {
      mCutOutSpeed = XMLHelper<double>::getValue( curr );
   }
   else if ( nodeName == sXMLTagNames[ FCR_KEY ] )
   {
      mFCR = XMLHelper<double>::getValue( curr );
   }
   else if ( nodeName == sXMLTagNames[ GRID_CONNECTION_COST_KEY ] )
   {
      mGridConnectionCost = XMLHelper<double>::getValue( curr );
   }
   else if ( nodeName == sXMLTagNames[ OM_KEY ] )
   {
      mOM = XMLHelper<double>::getValue( curr );
   }
   else if ( nodeName == sXMLTagNames[ ROTOR_DIAMETER_KEY ] )
   {
      mRotorDiameter = XMLHelper<double>::getValue( curr );
   }
   else if ( nodeName == sXMLTagNames[ TURBINE_DENSITY_KEY ] )
   {
      mTurbineDensity = XMLHelper<double>::getValue( curr );
   }
   else if ( nodeName == sXMLTagNames[ TURBINE_DERATING_KEY ] )
   {
      mTurbineDerating = XMLHelper<double>::getValue( curr );
   }
   else if ( nodeName == sXMLTagNames[ TURBINE_HUB_HEIGHT_KEY ] )
   {
      mTurbineHubHeight = XMLHelper<double>::getValue( curr );
   }
   else if ( nodeName == sXMLTagNames[ TURBINE_RATING_KEY ] )
   {
      mTurbineRating = XMLHelper<double>::getValue( curr );
   }
   else if ( nodeName == sXMLTagNames[ WIND_FARM_LOSS_KEY ] )
   {
      mWindFarmLoss = XMLHelper<double>::getValue( curr );
   }
   else
   {
      return false;
   }

   return true;
}


/*
   The one parameter Weibull distribution is (Equation A-1):
   f( x ) = ( B / c ) * ( x / c )^(B - 1) * e^-( x / c )^B

   The mean of the distribution is (Equation A-2):
   X = c * gamma * ( 1 / B + 1)

   The mean wind speed is (Equation A-3):
   X = c * gamma * 1.5 = ( sqrt( pi ) / 2 ) * c
   or
   c = X / a = 2X / sqrt( pi ) * a = sqrt( pi ) / 2 = 0.88623

   The wind distribution for a mean wind speed of is (Equation A-4):
   f( v ) = ( pi * v / 2X^2 ) * e^( -pi * ( v / 2X )^2

   Equation A-8:
   Xf = ( sqrt( pi ) / 2 ) * ( Vf / Vave )

   Equation A-10:
   1/2 * p * A * Cp * v^3 = 1 / 8 * p * D^2 * ( 16 / 27 ) * v^3 = Prated

   Equation A-18:
   (16 / 9 * pi) * Prb^2 * { 6 - [ Xr^6 + 3 Xr^4 + 6Xr^2 + 6 ]e^(-(Xr)^2) + Xr^6[ e^(-(Xr)^2) - e^(-(Xf)^2)]} = (16 / 9 * pi) * G( Xr, Xf ) * Prb^2

   Equation A-19:
   Var(P^2) = [ ( 16 / 9 * pi ) * G( Xr, Xf ) - CCfinite-power^2( Xr, Xf ) ] * Prb^2
*/
double WindTechnology::computeWindPowerVariance(
   double aAveWindSpeed,
    double aRating,
    double aDiameter,
    double aAirDensity,
    double aCutoutSpeed ) const
{
   static const double PI     = 2.0 * std::asin( 1.0 );

   // Equation A-18:
   // (16 / 9 * pi) * Prb^2 * { 6 - [ Xr^6 + 3 Xr^4 + 6Xr^2 + 6 ]e^(-(Xr)^2) + Xr^6[ e^(-(Xr)^2) - e^(-(Xf)^2)]} = (16 / 9 * pi) * G( Xr, Xf ) * Prb^2
   // Xr = rating
   // Xf = cutout speed
   double G = ( 6.0 - ( std::pow( aRating, 6.0 ) + 3.0 * std::pow( aRating, 4.0 ) + 6.0 * std::pow( aRating, 2.0 ) + 6.0 ) * std::exp( -std::pow( aRating, 2.0) ) + std::pow( aRating, 6.0 ) * ( std::exp( -std::pow( aRating, 2.0) ) - std::exp( -std::pow( aCutoutSpeed, 2.0 ) ) ) );

   // Equation A-19:
   // Var(P^2) = [ ( 16 / 9 * pi ) * G( Xr, Xf ) - CCfinite-power^2( Xr, Xf ) ] * Prb^2
   // Xr = rating
   // Xf = cutout speed
   double windPowerVariance = ( 16.0 / 9.0 * PI * G - std::pow( WindTechnology::calcTurbineCoefficient( aAveWindSpeed, aRating, aDiameter, aAirDensity, aCutoutSpeed ), 2.0 ) ) * std::pow( WindTechnology::calcIdealTurbineOutput( aAveWindSpeed, aDiameter, aAirDensity ), 2.0 );

   return windPowerVariance;
}

// end of wind_technology.cpp **********************************************
