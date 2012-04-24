#ifndef _MARKET_DEPENDENCY_FINDER_H_
#define _MARKET_DEPENDENCY_FINDER_H_
#if defined(_MSC_VER)
#pragma once
#endif

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



/*! 
 * \file market_dependency_finder.h
 * \ingroup Objects
 * \brief The MarketDependencyFinder class header file.
 * \author Pralit Patel
 */

#include <vector>
#include <string>
#include <set>

class Marketplace;
class IActivity;

/*! 
 * \ingroup Objects
 * \brief This class calculates an ordering of objects to calculate through a scheduling
 *        algorithm based on supplied dependencies.  Orderings can be obtained for
 *        all objects or only those affected by a change in a specified market.
 * \details The following steps are taken to create an in-order list of objects that
 *          may be directly called to calculate the model.
 *              - All objects must register their dependencies by name via the
 *                addDependency method.  Note that region names are required so
 *                that trading goods directly between regions is possible.
 *              - Bind activities such as sector, resources, etc to registered
 *                dependency names via resolveActivityToDependency.  Note that a
 *                dependency must have been added for the activity before binding
 *                the item.
 *              - Generate a global ordering via createOrdering.  Note that once
 *                called no more dependencies may be added.  Note that this will
 *                automatically resolve dependency cycles by creating trial markets.
 *                See Marketplace::resetToPriceMarket for details.  Also during
 *                this process markets will be bound to the activities which
 *                are directly affected by a change in the price of that market.
 *              - Get the global ordering via getOrdering() or a market specific
 *                ordering via getOrdering(int marketNumber).  Note that for market
 *                specific ordering a search will be performed to get a complete
 *                list of items to calculate.
 *
 * \author Pralit Patel
 */
class MarketDependencyFinder
{
public:
    MarketDependencyFinder( Marketplace* aMarketplace );
    ~MarketDependencyFinder();
    
    bool addDependency( const std::string& aDependentName,
                        const std::string& aDependentRegion,
                        const std::string& aDependencyName,
                        const std::string& aDependencyRegion );

    const std::vector<IActivity*> getOrdering( const int aMarketNumber = -1 ) const;

    void resolveActivityToDependency( const std::string& aRegionName, 
                                      const std::string& aActivityName,
                                      IActivity* aDemandActivity,
                                      IActivity* aPriceActivity = 0 );
    
    void createOrdering();
private:
    // CalcVertex and related declarations
    /*!
     * \brief The data structure which represents the final dependency graph.
     * \details  A vertex could represent either price or demand calculations.
     *           The out edges represent dependencies.
     */
    struct CalcVertex {
        CalcVertex( IActivity* aCalcItem ):mCalcItem( aCalcItem ) {}
        ~CalcVertex();
        
        //! The object which does the calculations for this vertex.
        IActivity* mCalcItem;
        
        //! The dependents of this vertex.
        std::vector<CalcVertex*> mOutEdges;
    };
    // Some typedefs to make the syntax of using vectors of calc vertices cleaner.
    typedef std::vector<CalcVertex*> VertexList;
    typedef VertexList::iterator VertexIterator;
    typedef VertexList::const_iterator CVertexIterator;
    
    // DependencyItem and related declarations
    struct DependencyItem;
    
    /*!
     * \brief A comparison functor to allow unique identification and sorting of
     *        DependencyItems.
     */
    struct DependencyItemComp {
        bool operator()( const DependencyItem* aLHS, const DependencyItem* aRHS ) const;
    };
    
    // Some typedefs to make the syntax of using DependencyItems cleaner.
    typedef std::set<DependencyItem*, DependencyItemComp> DependencyItemSet;
    typedef DependencyItemSet::iterator ItemIterator;
    typedef DependencyItemSet::const_iterator CItemIterator;
    
    /*!
     * \brief A struct to help hold together all of the information that we collect
     *        along the process of setting up the dependency graph.
     * \details All of the various information required to generate the dependency
     *          graph can be uniquely identified by a dependency name and the
     *          region in which that name is located.  Members of this data
     *          structure are fill in the follow way:
     *          1) mName, mLocatedInRegion, and mDependentList during the initial
     *             collection of dependencies via addDependency.
     *          2) mPriceVertices and mDemandVertices when activities are set from
     *             the regions via resolveActivityToDependency.
     *          3) mIsSolved, and mLinkedMarket during the first phase of createOrdering
     *             to associate markets to their corresponding DependencyItem.
     */
    struct DependencyItem {
        DependencyItem( const std::string& aName, const std::string& aLocatedInRegion )
        :mName( aName ), mLocatedInRegion( aLocatedInRegion ), mIsSolved( false ),
        mLinkedMarket( -1 ) {}
        ~DependencyItem();
        
        //! A name of a dependency which will correspond to a sector or resource, etc.
        const std::string mName;
        
        //! The name of the region in which this dependency's activities will
        //! reside.
        const std::string mLocatedInRegion;
        
        //! The initial linkage to dependent items which is put together potentially
        //! before the DependencyItem has it's activities set.
        std::set<DependencyItem*, DependencyItemComp> mDependentList;
        
        //! The vertices which represent the price calculation activities.
        VertexList mPriceVertices;
        
        //! The vertices which represent the demand calculation activities.
        VertexList mDemandVertices;
        
        //! Whether this item is associated with a market that is solved.
        bool mIsSolved;
        
        //! The market number to which this item is associated with.  Note that we
        //! do not directly link to a Market* since they are created by model period
        //! and this graph will be static through all model periods.
        int mLinkedMarket;
        
        /*!
         * \brief Helper function to clean up syntax in accessing the first price
         *        vertex in this dependency item.
         * \return The first price vertex.
         */
        CalcVertex* getFirstPriceVertex() const {
            return *mPriceVertices.begin();
        }
        
        /*!
         * \brief Helper function to clean up syntax in accessing the last price
         *        vertex in this dependency item.
         * \return The last price vertex.
         */
        CalcVertex* getLastPriceVertex() const {
            return *mPriceVertices.rbegin();
        }
        
        /*!
         * \brief Helper function to clean up syntax in accessing the first demand
         *        vertex in this dependency item.
         * \return The first demand vertex.
         */
        CalcVertex* getFirstDemandVertex() const {
            return *mDemandVertices.rbegin();
        }
        
        /*!
         * \brief Helper function to clean up syntax in accessing the last demand
         *        vertex in this dependency item.
         * \return The last demand vertex.
         */
        CalcVertex* getLastDemandVertex() const {
            return *mDemandVertices.begin();
        }
    };
    
    //! The set of all traced dependencies in the model.
    DependencyItemSet mDependencyItems;
    
    // MarketToDependencyItem and related declarations
    /*!
     * \brief A helpful struct to provide a link between a market and the vertices
     *        which would need to recalculate if the solver changed it's price.
     */
    struct MarketToDependencyItem {
        MarketToDependencyItem( const int aMarketNumber ):mMarket( aMarketNumber ) {}
        
        //! The market number which this struct represents.  Note that we do not
        //! directly link to a Market* since they are created by model period
        //! and the graph will be static through all model periods.
        const int mMarket;
        
        //! A unique set of vertices to re-calculate should this market change
        //! it's price.
        std::set<CalcVertex*> mImpliedVertices;
    };
    
    /*!
     * \brief A comparison functor to allow unique identification and sorting of
     *        MarketToDependencyItem.
     */
    struct MarketToDependencyItemComp {
        bool operator()( const MarketToDependencyItem* aLHS, const MarketToDependencyItem* aRHS ) const {
            return aLHS->mMarket < aRHS->mMarket;
        }
    };
    // Some typedefs to make the syntax of using MarketToDependencyItems cleaner.
    typedef std::set<MarketToDependencyItem*, MarketToDependencyItemComp> MarketToDependencyItemSet;
    typedef MarketToDependencyItemSet::iterator MarketToDepIterator;
    typedef MarketToDependencyItemSet::const_iterator CMarketToDepIterator;
    
    //! The set of all solved markets which would need to implicitly call calc
    //! vertices when changing it's price.
    MarketToDependencyItemSet mMarketsToDep;
        
    //! A weak pointer to the marketplace.
    Marketplace* mMarketplace;
    
    //! The final global ordering
    std::vector<IActivity*> mGlobalOrdering;
    
    void findVerticesToCalculate( CalcVertex* aVertex, std::set<IActivity*>& aVisited ) const;
    int markCycles( CalcVertex* aCurrVertex, std::list<CalcVertex*>& aHasVisited, std::map<CalcVertex*, int>& aTotalVisists ) const;
};

#endif // _MARKET_DEPENDENCY_FINDER_H_
