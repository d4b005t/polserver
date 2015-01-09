/*
History
=======
2005/01/23 Shinigami: cleanup_vars - Tokuno MapDimension doesn't fit blocks of 64x64 (WGRID_SIZE)
2008/02/09 Shinigami: removed hard coded MAX_CHARS from cleanup_vars()
2009/01/18 Nando:     Realms was not being freed when exiting POL
                      vitals too. Multidefs too. Oh well...
                      Moved objecthash.ClearCharacterAccountReferences() out of the realms' loop
                      Added clean_multidefs()
2009/09/03 MuadDib:   Relocation of account related cpp/h
                      Changes for multi related source file relocation

Notes
=======

*/
#include "uvars.h"
#include "multidefs.h"
#include "script_internals.h"
#include "network.h"
#include "settings.h"
#include "state.h"
#include "object_storage.h"
#include "ucfg.h"

#include "../../clib/clib.h"
#include "../../clib/logfacility.h"
#include "../../clib/MD5.h"
#include "../../clib/stlutil.h"

#include "../../plib/systemstate.h"

#include "../accounts/account.h"
#include "../accounts/accounts.h"
#include "../checkpnt.h"
#include "../cmdlevel.h"
#include "../console.h"
#include "../guardrgn.h"
#include "../guilds.h"
#include "../item/item.h"
#include "../item/itemdesc.h"
#include "../item/weapon.h"
#include "../listenpt.h"
#include "../loadunld.h"
#include "../miscrgn.h"
#include "../mobile/attribute.h"
#include "../mobile/charactr.h"
#include "../multi/boat.h"
#include "../multi/multi.h"
#include "../musicrgn.h"
#include "../npctmpl.h"
#include "../party.h"
#include "../polsem.h"
#include "../proplist.h"
#include "../realms.h"
#include "../resource.h"
#include "../scrstore.h"
#include "../sockio.h"
#include "../spells.h"
#include "../startloc.h"
#include "../storage.h"
#include "../target.h"
#include "../tasks.h"
#include "../ufunc.h"
#include "../uoskills.h"
#include "../uworld.h"
#include "../vital.h"

#ifdef _MSC_VER
#pragma warning(disable:4505) // '...': unreferenced local function has been removed (because of region.h)
#endif

namespace Pol {
  namespace Core {

	GameState gamestate;


	GameState::GameState() :
	  cmdlevels(),
	  npc_templates(),
	  npc_template_elems(),
	  global_properties(new Core::PropertyList),
	  accounts(),
	  startlocations(),
	  wrestling_weapon(nullptr),
	  justicedef(nullptr),
	  nocastdef(nullptr),
	  lightdef(nullptr),
	  weatherdef(nullptr),
	  musicdef(nullptr),
	  menus(),
	  storage(),
	  parties(),
	  guilds(),
	  nextguildid(1),
	  main_realm(nullptr),
	  Realms(),
	  shadowrealms_by_id(),
	  baserealm_count(0),
	  shadowrealm_count(0),

	  update_rpm_task(new PeriodicTask(update_rpm, 60, "RPM")),
	  regen_stats_task(new PeriodicTask(regen_stats, 5, "Regen")),
	  regen_resources_task(new PeriodicTask(regen_resources, 10, 60 * 10, "RsrcRegen")),
	  reload_accounts_task(new PeriodicTask(Accounts::reload_account_data, 30, "LOADACCT")),
	  write_account_task(new PeriodicTask(Accounts::write_account_data_task, 60, "WRITEACCT")),
	  update_sysload_task(new PeriodicTask(update_sysload, 1, "SYSLOAD")),
	  reload_pol_cfg_task(new PeriodicTask(PolConfig::reload_pol_cfg, 30, "LOADPOLCFG")),

	  attributes(),
	  numAttributes(0),
	  attributes_byname(),
	  pAttrStrength(nullptr),
	  pAttrIntelligence(nullptr),
	  pAttrDexterity(nullptr),
	  pAttrParry(nullptr),
	  pAttrTactics(nullptr),

	  // Magery is repeated at array entry 3, because as of right now, NO spellbook
	  // on OSI uses the 301+ spellrange that we can find. 5/30/06 - MuadDib
	  // We use Mysticism at array entry 3 because Mysticism spellids are 678 -> 693 and this slot is free.
	  spell_scroll_objtype_limits ({{ // TODO: Comment those objtypes :D
		{ { 0x1F2D, 0x1F6C }},
		{{ 0x2260, 0x226F }},
		{{ 0x2270, 0x227C }},
		{{ 0x2D9E, 0x2DAD }},
		{{ 0x238D, 0x2392 }},
		{{ 0x23A1, 0x23A8 }},
		{{ 0x2D51, 0x2D60 }},
		{{ 0x574B, 0x5750 }} 
		}}),
	  spells(),
	  spellcircles(),
	  export_scripts(),
	  system_hooks(),
	  tipfilenames(),
	  armorzones(),
	  armor_zone_chance_sum(0),
	  vitals(),
	  numVitals(0),
	  pVitalLife(nullptr),
	  pVitalStamina(nullptr),
	  pVitalMana(nullptr),
	  vitals_byname(),
	  desctable(),
	  old_objtype_conversions(),
	  dynamic_item_descriptors(),
	  objtype_byname(),
	  // The temp_itemdesc is used when something is created by graphic.
	  // another option is to create such ItemDesc objects on demand as needed, and keep them around.
	  empty_itemdesc(new Items::ItemDesc(Items::ItemDesc::ITEMDESC)),
	  temp_itemdesc(new Items::ItemDesc(Items::ItemDesc::ITEMDESC)),
	  resourcedefs(),
	  
	  intrinsic_weapons(),
	  boatshapes(),

	  animation_translates(),
	  console_commands(),
	  landtiles(),
	  landtiles_loaded(false),
	  listen_points(),
	  wwwroot_pkg(nullptr),
	  mime_types(),
	  task_queue(),
	  Global_Ignore_CProps(),
	  target_cursors(),
	  textcmds(),
	  paramtextcmds(),
	  uo_skills()
	{
	  memset( &mount_action_xlate, 0, sizeof( mount_action_xlate ) );
	}
	GameState::~GameState()
	{
	  // FIXME: since deconstruction of externs has a more or less random order
	  // everything should be cleared before.
	  // or make sure that the globals get deconstructed before eg the flyweight string container
	}

	void display_leftover_objects();

	void GameState::deinitialize()
	{
	  INFO_PRINT << "Initiating POL Cleanup....\n";

	  networkManager.deinialize();
	  deinit_ipc_vars();

	  if ( Plib::systemstate.config.log_script_cycles )
		log_all_script_cycle_counts( false );

	  checkpoint( "cleaning up vars" );
	  cleanup_vars();
	  checkpoint( "cleaning up scripts" );
	  cleanup_scripts();

	  // scripts remove their listening points when they exit..
	  // so there should be no listening points to clean up.
	  checkpoint( "cleaning listen points" );
	  clear_listen_points();

	  

	  // unload_other_objects
	  unload_intrinsic_weapons();
      unload_weapon_templates( );

	  // unload_itemdesc_scripts
	  for ( auto &elem : desctable )
	  {
		elem.second->unload_scripts();
	  }

	  system_hooks.unload_system_hooks();

	  configurationbuffer.deinitialize();

	  Plib::systemstate.deinitialize();
	  Multi::multidef_buffer.deinitialize();

	  unload_npc_templates();  //quick and nasty fix until npcdesc usage is rewritten Turley 2012-08-27: moved before objecthash due to npc-method_script cleanup

	  Bscript::UninitObject::ReleaseSharedInstance();
	  objStorageManager.deinitialize();
	  display_leftover_objects();

	  checkpoint( "unloading data" );
	  unload_data();
	  guilds.clear();

	  Clib::MD5_Cleanup();

	  checkpoint( "misc cleanup" );

	  global_properties->clear();
	  menus.clear();

	  textcmds.clear();
	  paramtextcmds.clear();
	  Global_Ignore_CProps.clear();
	  mime_types.clear();
	  console_commands.clear();
	  animation_translates.clear();
	  
	  tipfilenames.clear();

	  checkpoint( "end of xmain2" );

#ifdef __linux__
	  unlink( ( Plib::systemstate.config.pidfile_path + "pol.pid" ).c_str( ) );
#endif
	}

	

	void GameState::cleanup_vars()
	{
	  //dave added 9/27/03: accounts and player characters have a mutual reference that prevents them getting cleaned up
	  //  properly. So clear the references now.
      for ( auto &account : accounts )
      {
        for ( int i = 0; i < Plib::systemstate.config.character_slots; i++ )
          account->clear_character( i );
      }

	  for ( auto &realm : Realms)
	  {
		unsigned wgridx = realm->grid_width();
		unsigned wgridy = realm->grid_height();

		for ( unsigned wx = 0; wx < wgridx; ++wx )
		{
          for ( unsigned wy = 0; wy < wgridy; ++wy )
          {
            for ( auto &item : realm->zone[wx][wy].items )
            {
              world_delete( item );
            }
            realm->zone[wx][wy].items.clear();
          }
		}

		for ( unsigned wx = 0; wx < wgridx; ++wx )
		{
          for ( unsigned wy = 0; wy < wgridy; ++wy )
          {
            for ( auto &chr : realm->zone[wx][wy].characters )
            {
              chr->acct.clear(); //dave added 9/27/03, see above comment re: mutual references
              world_delete( chr );
            }
            realm->zone[wx][wy].characters.clear();
            for ( auto &chr : realm->zone[wx][wy].npcs )
            {
              chr->acct.clear(); //dave added 9/27/03, see above comment re: mutual references
              world_delete( chr );
            }
            realm->zone[wx][wy].npcs.clear();
          }
		}

		for ( unsigned wx = 0; wx < wgridx; ++wx )
		{
		  for ( unsigned wy = 0; wy < wgridy; ++wy )
		  {
            for ( auto &multi : realm->zone[wx][wy].multis )
			{
			  world_delete( multi );
			}
            realm->zone[wx][wy].multis.clear();
		  }
		}
	  }

	  // dave renamed this 9/27/03, so we only have to traverse the objhash once, to clear out account references and delete.
	  // and Nando placed it outside the Realms' loop in 2009-01-18. 
	  objStorageManager.objecthash.ClearCharacterAccountReferences();

	  accounts.clear();
	  Clib::delete_all( startlocations );

	  storage.clear();

	  //RegionGroup cleanup _before_ Realm cleanup
	  delete justicedef;
	  delete lightdef;
	  delete nocastdef;
	  delete weatherdef;
	  delete musicdef;
	  clean_resources();

	  Clib::delete_all( Realms );
	  shadowrealms_by_id.clear();
	  main_realm = nullptr;

	  //delete_all(vitals);
	  clean_vitals();
	  Multi::clean_boatshapes();

	  Mobile::clean_attributes();
	  cmdlevels.clear();
	  clean_spells();
	  clean_skills();
	}

	// Note, when the program exits, each executor in these queues
	// will be deleted by cleanup_scripts()
	// Therefore, any object that owns an executor must be destroyed 
	// before cleanup_scripts() is called.

	void GameState::cleanup_scripts()
	{
	  scriptEngineInternalManager.deinitialize();
	}

	void GameState::clear_listen_points()
	{
	  for ( auto & lp_pair : listen_points )
	  {
		ListenPoint* lp = lp_pair.second;
		delete lp;
		lp_pair.second = NULL;
	  }
	  listen_points.clear();
	}

	void GameState::unload_intrinsic_weapons()
	{
      if ( wrestling_weapon != NULL )
	  {
        wrestling_weapon->destroy( );
        wrestling_weapon = NULL;
	  }
	}

	void GameState::unload_weapon_templates()
	{
      for ( auto &weapon : intrinsic_weapons )
      {
        //	t.second->serial = 1; // just to force the delete to work.
        if ( weapon.second != NULL )
        {
          weapon.second->destroy( );
          weapon.second = NULL;
        }
      }
	  intrinsic_weapons.clear();
	}

	//quick and nasty fix until npcdesc usage is rewritten
	void GameState::unload_npc_templates()
	{
	  for ( auto & templates : npc_templates )
	  {
		if ( templates.second != NULL )
		  delete templates.second;
		templates.second = NULL;
	  }
	  npc_templates.clear();

	  npc_template_elems.clear();
	}

  }
}