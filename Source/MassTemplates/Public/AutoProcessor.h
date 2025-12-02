#pragma once

#include "MassProcessor.h"
#include "MassEntityTypes.h"
#include "MassEntityElementTypes.h"
#include "MassCommonFragments.h"
#include "MassEntityQuery.h"
#include "CoreMinimal.h"
#include "MassExecutionContext.h"
#include <concepts>
#include <utility>
#include <format>

// Concept to ensure T is a valid Mass fragment

// this takes a fragment directly, all the other concepts take a FFragmentRequirement
template < typename F >
concept IsDerivedFromFragmentOrTag = TIsDerivedFrom< F, FMassFragment >::IsDerived || TIsDerivedFrom< F, FMassSharedFragment >::IsDerived || TIsDerivedFrom< F, FMassConstSharedFragment >::IsDerived ||
									 TIsDerivedFrom< F, FMassTag >::IsDerived || TIsDerivedFrom< F, USubsystem >::IsDerived;

// Struct to define fragment requirements with C++20 concepts
// this requirement will fail if the fragment type is only forward declared
template < typename T, EMassFragmentAccess InAccess = EMassFragmentAccess::ReadOnly, EMassFragmentPresence InPresence = EMassFragmentPresence::All >
requires IsDerivedFromFragmentOrTag< T >
struct FFragmentRequirement
{
	using FragmentType = T;
	static constexpr EMassFragmentAccess Access = InAccess;
	static constexpr EMassFragmentPresence Presence = InPresence;
};

template < typename T >
concept IsNormalFragment = TIsDerivedFrom< typename T::FragmentType, FMassFragment >::IsDerived;

template < typename T >
concept IsSharedFragment = TIsDerivedFrom< typename T::FragmentType, FMassSharedFragment >::IsDerived;

template < typename T >
concept IsConstSharedFragment = TIsDerivedFrom< typename T::FragmentType, FMassConstSharedFragment >::IsDerived;

template < typename T >
concept IsMassTag = TIsDerivedFrom< typename T::FragmentType, FMassTag >::IsDerived;

template < typename T >
concept IsSubsystem = TIsDerivedFrom< typename T::FragmentType, USubsystem >::IsDerived;

template < typename T >
concept IsMassFragment = IsNormalFragment< T > || IsSharedFragment< T > || IsConstSharedFragment< T >;

template < typename T >
concept IsMassFragmentOrTag = IsMassFragment< T > || IsMassTag< T >;

template < typename T >
concept IsOptional = T::Presence == EMassFragmentPresence::Any || T::Presence == EMassFragmentPresence::Optional;

template < typename T >
concept IsPresenceNone = T::Presence == EMassFragmentPresence::None;

template < typename T >
concept IsReadWrite = T::Access == EMassFragmentAccess::ReadWrite;

// TAutoMassProcessor cannot be templated because of UHT and multiple inheritance fails to work `UObject::UObject()->EnsureNotRetrievingVTablePtr()]
// and it has to be a UCLASS to get registered [?] so template a member instead

template < typename... Requirements >
struct FExtendedMassQuery : public FMassEntityQuery
{
	// we have a FMassEntityQuery member and in our constructor we add requirements
	// the TAutoMassProcessor derived class has a FEntityQueryWrapper member

  private:
	template < typename T >
	void AddSingleRequirement()
	{

		// add a requirement to the member query
		if constexpr ( IsNormalFragment< T > )
		{
			constexpr auto Predicate = []( const FMassFragmentRequirementDescription& Item )
			{
				using SS = typename T::FragmentType;
				return Item.StructType == SS::StaticStruct();

			};

			if ( FragmentRequirements.FindByPredicate( Predicate ) )
			{
				UE_LOG( LogTemp, Warning, TEXT( "Attempt to add duplicate fragment requirement. %s already present" ), *T::FragmentType::StaticStruct()->GetName() );
				return;
			}

			AddRequirement< typename T::FragmentType >( T::Access, T::Presence );
		}
		else if constexpr ( IsSharedFragment< T > )
		{
			constexpr auto Predicate = []( const FMassFragmentRequirementDescription& Item )
			{
				using SS = typename T::FragmentType;
				return Item.StructType == typename SS::StaticStruct();
			};

			if ( SharedFragmentRequirements.FindByPredicate( Predicate ) )
			{
				UE_LOG( LogTemp, Warning, TEXT( "Attempt to add duplicate shared fragment requirement. %s already present" ), *typename T::FragmentType::StaticStruct()->GetName() );
				return;
			}

			AddSharedRequirement< typename T::FragmentType >( T::Access, T::Presence );
		}
		else if constexpr ( IsConstSharedFragment< T > )
		{
			constexpr auto Predicate = []( const FMassFragmentRequirementDescription& Item )
			{
				using SS = typename T::FragmentType;
				return Item.StructType == typename SS::StaticStruct();
			};

			if ( ConstSharedFragmentRequirements.FindByPredicate( Predicate ) )
			{
				UE_LOG( LogTemp, Warning, TEXT( "Attempt to add duplicate const shared fragment requirement. %s already present" ), *typename T::FragmentType::StaticStruct()->GetName() );
				return;
			}

			AddConstSharedRequirement< typename T::FragmentType >( T::Presence );
		}
		else if constexpr ( IsMassTag< T > )
		{
			AddTagRequirement< typename T::FragmentType >( T::Presence );
		}
		else if constexpr ( IsSubsystem< T > )
		{
			AddSubsystemRequirement< typename T::FragmentType >( T::Access );
		}
	}

  public:
  public:
	FExtendedMassQuery() = default;

	FExtendedMassQuery( const TSharedPtr< FMassEntityManager >& EntityManager )
		: FMassEntityQuery( EntityManager )
	{
		// add requirements
		( AddSingleRequirement< Requirements >(), ... );
	}

	// make a list of the return types so we can have refgerences for madatory items
	// and pointers for optional types differently, and const versions
	template < typename T >
	requires IsMassFragment< T > || IsSubsystem< T >
	static constexpr auto GetFragmentType()
	{
		if constexpr ( IsSubsystem< T > )
		{
			if constexpr ( IsReadWrite< T > )
			{
				return std::tuple< std::add_pointer_t< USubsystem > >();
			}
			else
			{
				return std::tuple< std::add_const_t< std::add_pointer_t< USubsystem > > >();
			}
		}
		else if constexpr ( IsPresenceNone< T > )
		{
			// don't add a type to the list of data types
			return std::tuple<>();
		}
		else if constexpr ( IsOptional< T > )
		{
			if constexpr ( IsReadWrite< T > )
			{
				return std::tuple< std::add_pointer_t< typename T::FragmentType > >();
			}
			else
			{
				return std::tuple< std::add_const_t< std::add_pointer_t< typename T::FragmentType > > >();
			}
		}
		else if constexpr ( !IsReadWrite< T > )
		{
			return std::tuple< std::add_const_t< typename T::FragmentType > >();
		}
		else
		{
			return std::tuple< typename T::FragmentType >();
		}
	}

	template < typename T >
	requires IsMassTag< T >
	static constexpr auto GetFragmentType()
	{
		// don't return a type, we never retrieve data for a tag
		return std::tuple<>();
	}

	using ViewDataTypes = decltype( std::tuple_cat( GetFragmentType< Requirements >()... ) );

  private:
	//
	// GetView calls
	//

	template < class... >
	struct False : std::bool_constant< false >
	{
	};

	template < typename T >
	auto GetView( FMassExecutionContext& InContext )
	{
		// add a requirement to the member query
		if constexpr ( IsSubsystem< T > )
		{
			if constexpr ( IsReadWrite< T > )
			{
				return std::make_tuple<>( InContext.GetMutableSubsystem() );
			}
			else
			{
				return std::make_tuple<>( InContext.GetSubsystem() );
			}
		}
		else if constexpr ( IsPresenceNone< T > )
		{
			return std::make_tuple<>();
		}
		else if constexpr ( IsNormalFragment< T > && IsReadWrite< T > )
		{
			// return reference to view
			return std::make_tuple( InContext.GetMutableFragmentView< typename T::FragmentType >() );
		}
		else if constexpr ( IsNormalFragment< T > && !IsReadWrite< T > )
		{
			// return reference to view
			return std::make_tuple( InContext.GetFragmentView< typename T::FragmentType >() );
		}
		else if constexpr ( IsConstSharedFragment< T > && !IsOptional< T > )
		{
			// shared fragments if mandatory calll GetConstSharedFragment which returns a reference to the one fragment
			// if optional call GetConstSharedFragmentPtr which returns a pointer
			// this returns a pointer
			return std::make_tuple( InContext.GetConstSharedFragment< T::FragmentType >() );
		}
		else if constexpr ( IsConstSharedFragment< T > && IsOptional< T > )
		{
			// this returns a pointer
			return std::make_tuple( InContext.GetConstSharedFragmentPtr< T::FragmentType >() );
		}
		else if constexpr ( IsMassTag< T > )
		{
			return std::tuple<>();
		}
		else
		{
			/// static_assert( False< int >{}, "no matching GetView call" );
		}
	}

	// GetFragmentFromView get the view data which might be an array of values
	// or a const array, or a single value for shared fragments

	template < typename FinalType >
	auto GetFragmentFromView( FMassExecutionContext& InContext, TArrayView< FinalType >& View, const size_t EntityIndex )
	{
		return std::tie( View[EntityIndex] );
	}

	template < typename FinalType >
	requires std::is_pointer_v< FinalType >
	auto GetFragmentFromView( FMassExecutionContext& InContext, TArrayView< std::remove_pointer_t< FinalType > >& View, const size_t EntityIndex )
	{
		// its optional, output a pointer to the thing or null
		// TODO use std::tie to nullptr?
		return std::tuple( View.Num() > EntityIndex ? &View[EntityIndex] : nullptr );
	}

	template < typename FinalType >
	requires std::is_pointer_v< FinalType >
	auto GetFragmentFromView( FMassExecutionContext& InContext, TArrayView< const std::remove_pointer_t< FinalType > >& View, const size_t EntityIndex )
	{
		// its optional, output a pointer to the thing or null
		// TODO use std::tie to nullptr?
		return std::tuple( View.Num() > EntityIndex ? &View[EntityIndex] : nullptr );
	}

	template < typename FinalType >
	auto GetFragmentFromView( FMassExecutionContext& InContext, TArrayView< const FinalType >& View, const size_t EntityIndex )
	{
		return std::tie( View[EntityIndex] );
	}

	template < typename FinalType >
	requires std::is_pointer_v< FinalType > && std::is_const_v< FinalType >
	auto GetFragmentFromView( FMassExecutionContext& InContext, const std::remove_pointer_t< FinalType >* View, const size_t EntityIndex )
	{
		return std::tie( View );
	}

#if 0
	// test with "auto View" and log the actual type
	template < typename FinalType >
		requires std::is_pointer_v< FinalType >&& std::is_const_v< FinalType >
	auto GetFragmentFromViewX(FMassExecutionContext& InContext, auto View, const size_t EntityIndex)
	{
		// its optional, shared or shared const so no array just a pointer

		auto FinalTypeType = typeid(FinalType).name();
		auto ViewType = typeid(View).name();

		// FinalType             struct FMassMovementParameters* __ptr64
		// ViewType              struct FMassMovementParameters const* __ptr64

		UE_LOG(LogTemp, Log, TEXT("FinalType %hs ViewType %hs"), FinalTypeType, ViewType);
		return std::tie(View);
	}
#endif

	template < std::size_t... Is >
	auto ExpandWithIndex( std::index_sequence< Is... >, FMassExecutionContext& Context, auto&& Views, const size_t EntityIndex )
	{
		// iterate through the views extracting the data for 1 entity
		// std::tuple_element_t< Is, Requirements is a TArrayView or a pointer
		return std::tuple_cat( GetFragmentFromView< std::tuple_element_t< Is, ViewDataTypes > >( Context, std::get< Is >( Views ), EntityIndex )... );
	}

  public:
	void Execute( FMassEntityManager& EntityManager, FMassExecutionContext& Context, auto&& Func )
	{

		ForEachEntityChunk( EntityManager,
							Context,
							[&]( FMassExecutionContext& Context )
		{
			// what this does is:
			// - iterate the Requirements template paramater and get the data types for views into ViewDataTypes (TArrayView, TConstArrayView etc)
			// - iterate the Requirements template paramater and get the views into FragmentViews
			//
			// Then for each entity
			// - iterate the FragmentViews and get the Fragments for the entity
			// - called the passing function
			//
			// this is slightly brittle because it relies on the code for getting ViewDataTypes skipping types we don't
			// get such as Presence=None or Tags, and the same elements being skipped in the calculate FragmentViews code
			//

			// get views for each requirement which is not a tag
			auto FragmentViews = std::tuple_cat( GetView< Requirements >( Context )... );

			// some requirements are tags so don't create views
			static_assert( std::tuple_size_v< decltype( FragmentViews ) > <= sizeof...( Requirements ) );
			static_assert( std::tuple_size_v< decltype( FragmentViews ) > == std::tuple_size_v< ViewDataTypes > );

			using IndexSequence = std::make_index_sequence< std::tuple_size_v< ViewDataTypes > >;

			std::apply(
				[&]( auto&... Views )
			{
				int32 NumEntities = Context.GetNumEntities();

				for ( int32 i = 0; i < NumEntities; ++i )
				{
					auto FragmentsForEntity = ExpandWithIndex( IndexSequence{}, Context, FragmentViews, i );

					const FMassEntityHandle Entity = Context.GetEntity( i );

					Func( Entity, FragmentsForEntity );
				}
			},
				FragmentViews );
		} );
	}
};
