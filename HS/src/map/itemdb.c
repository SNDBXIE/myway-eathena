// Copyright (c) Hercules Dev Team, licensed under GNU GPL.
// See the LICENSE file
// Portions Copyright (c) Athena Dev Teams

#include "../common/nullpo.h"
#include "../common/malloc.h"
#include "../common/random.h"
#include "../common/showmsg.h"
#include "../common/strlib.h"
#include "../common/utils.h"
#include "../common/conf.h"
#include "itemdb.h"
#include "map.h"
#include "battle.h" // struct battle_config
#include "script.h" // item script processing
#include "pc.h"     // W_MUSICAL, W_WHIP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct item_data* itemdb_array[MAX_ITEMDB];
static DBMap*            itemdb_other;// int nameid -> struct item_data*

struct item_data dummy_item; //This is the default dummy item used for non-existant items. [Skotlex]

struct itemdb_interface itemdb_s;

/**
 * Search for item name 
 * name = item alias, so we should find items aliases first. if not found then look for "jname" (full name)
 * @see DBApply
 */
static int itemdb_searchname_sub(DBKey key, DBData *data, va_list ap)
{
	struct item_data *item = DB->data2ptr(data), **dst, **dst2;
	char *str;
	str=va_arg(ap,char *);
	dst=va_arg(ap,struct item_data **);
	dst2=va_arg(ap,struct item_data **);
	if(item == &dummy_item) return 0;

	//Absolute priority to Aegis code name.
	if (*dst != NULL) return 0;
	if( strcmpi(item->name,str)==0 )
		*dst=item;

	//Second priority to Client displayed name.
	if (*dst2 != NULL) return 0;
	if( strcmpi(item->jname,str)==0 )
		*dst2=item;
	return 0;
}

/*==========================================
 * Return item data from item name. (lookup) 
 *------------------------------------------*/
struct item_data* itemdb_searchname(const char *str) {
	struct item_data* item;
	struct item_data* item2=NULL;
	int i;

	for( i = 0; i < ARRAYLENGTH(itemdb_array); ++i ) {
		item = itemdb_array[i];
		if( item == NULL )
			continue;

		// Absolute priority to Aegis code name.
		if( strcasecmp(item->name,str) == 0 )
			return item;

		//Second priority to Client displayed name.
		if( strcasecmp(item->jname,str) == 0 )
			item2 = item;
	}

	item = NULL;
	itemdb_other->foreach(itemdb_other,itemdb_searchname_sub,str,&item,&item2);
	return item?item:item2;
}
/* name to item data */
struct item_data* itemdb_name2id(const char *str) {
	return strdb_get(itemdb->names,str);
}

/**
 * @see DBMatcher
 */
static int itemdb_searchname_array_sub(DBKey key, DBData data, va_list ap)
{
	struct item_data *item = DB->data2ptr(&data);
	char *str;
	str=va_arg(ap,char *);
	if (item == &dummy_item)
		return 1; //Invalid item.
	if(stristr(item->jname,str))
		return 0;
	if(stristr(item->name,str))
		return 0;
	return strcmpi(item->jname,str);
}

/*==========================================
 * Founds up to N matches. Returns number of matches [Skotlex]
 *------------------------------------------*/
int itemdb_searchname_array(struct item_data** data, int size, const char *str) {
	struct item_data* item;
	int i;
	int count=0;

	// Search in the array
	for( i = 0; i < ARRAYLENGTH(itemdb_array); ++i )
	{
		item = itemdb_array[i];
		if( item == NULL )
			continue;

		if( stristr(item->jname,str) || stristr(item->name,str) )
		{
			if( count < size )
				data[count] = item;
			++count;
		}
	}

	// search in the db
	if( count < size )
	{
		DBData *db_data[MAX_SEARCH];
		int db_count = 0;
		size -= count;
		db_count = itemdb_other->getall(itemdb_other, (DBData**)&db_data, size, itemdb_searchname_array_sub, str);
		for (i = 0; i < db_count; i++)
			data[count++] = DB->data2ptr(db_data[i]);
		count += db_count;
	}
	return count;
}
/* [Ind/Hercules] */
int itemdb_chain_item(unsigned short chain_id, int *rate) {
	struct item_chain_entry *entry;
	int i = 0;
	
	if( chain_id >= itemdb->chain_count ) {
		ShowError("itemdb_chain_item: unknown chain id %d\n", chain_id);
		return UNKNOWN_ITEM_ID;
	}
	
	entry = &itemdb->chains[chain_id].items[ rnd()%itemdb->chains[chain_id].qty ];
	
	for( i = 0; i < itemdb->chains[chain_id].qty; i++ ) {
		if( rnd()%10000 >= entry->rate ) {
			entry = entry->next;
			continue;
		} else {
			if( rate )
				rate[0] = entry->rate;
			return entry->id;
		}
	}
	
	return 0;
}
/* [Ind/Hercules] */
void itemdb_package_item(struct map_session_data *sd, struct item_package *package) {
	int i = 0, get_count, j, flag;
	
	for( i = 0; i < package->must_qty; i++ ) {
		struct item it;
		memset(&it, 0, sizeof(it));

		it.nameid = package->must_items[i].id;
		it.identify = 1;
		
		if( package->must_items[i].hours ) {
			it.expire_time = (unsigned int)(time(NULL) + ((package->must_items[i].hours*60)*60));
		}
		
		if( package->must_items[i].named ) {
			it.card[0] = CARD0_FORGE;
			it.card[1] = 0;
			it.card[2] = GetWord(sd->status.char_id, 0);
			it.card[3] = GetWord(sd->status.char_id, 1);
		}
		
		if( package->must_items[i].announce )
			clif->package_announce(sd,package->must_items[i].id,package->id);
		
		get_count = itemdb_isstackable(package->must_items[i].id) ? package->must_items[i].qty : 1;
		
		it.amount = get_count == 1 ? 1 : get_count;
		
		for( j = 0; j < package->must_items[i].qty; j += get_count ) {
			if ( ( flag = pc->additem(sd, &it, get_count, LOG_TYPE_SCRIPT) ) )
				clif->additem(sd, 0, 0, flag);
		}
	}
	
	if( package->random_qty ) {
		struct item_package_rand_entry *entry;
		
		entry = &package->random_list[rnd()%package->random_qty];
		
		while( 1 ) {
			if( rnd()%10000 >= entry->rate ) {
				entry = entry->next;
				continue;
			} else {
				struct item it;
				memset(&it, 0, sizeof(it));
				
				it.nameid = entry->id;
				it.identify = 1;
				
				if( entry->hours ) {
					it.expire_time = (unsigned int)(time(NULL) + ((entry->hours*60)*60));
				}
				
				if( entry->named ) {
					it.card[0] = CARD0_FORGE;
					it.card[1] = 0;
					it.card[2] = GetWord(sd->status.char_id, 0);
					it.card[3] = GetWord(sd->status.char_id, 1);
				}
				
				if( entry->announce )
					clif->package_announce(sd,entry->id,package->id);
				
				get_count = itemdb_isstackable(entry->id) ? entry->qty : 1;
				
				it.amount = get_count == 1 ? 1 : get_count;
				
				for( j = 0; j < entry->qty; j += get_count ) {
					if ( ( flag = pc->additem(sd, &it, get_count, LOG_TYPE_SCRIPT) ) )
						clif->additem(sd, 0, 0, flag);
				}
				break;
			}
		}
	}
	
	return;
}
/*==========================================
 * Return a random item id from group. (takes into account % chance giving/tot group) 
 *------------------------------------------*/
int itemdb_searchrandomid(struct item_group *group) {

	if (group->qty)
		return group->nameid[rnd()%group->qty];
	
	ShowError("itemdb_searchrandomid: No item entries for group id %d\n", group->id);
	return UNKNOWN_ITEM_ID;
}
bool itemdb_in_group(struct item_group *group, int nameid) {
	int i;
	
	for( i = 0; i < group->qty; i++ )
		if( group->nameid[i] == nameid )
			return true;
	
	return false;
}

/// Searches for the item_data.
/// Returns the item_data or NULL if it does not exist.
struct item_data* itemdb_exists(int nameid)
{
	struct item_data* item;

	if( nameid >= 0 && nameid < ARRAYLENGTH(itemdb_array) )
		return itemdb_array[nameid];
	item = (struct item_data*)idb_get(itemdb_other,nameid);
	if( item == &dummy_item )
		return NULL;// dummy data, doesn't exist
	return item;
}

/// Returns human readable name for given item type.
/// @param type Type id to retrieve name for ( IT_* ).
const char* itemdb_typename(int type)
{
	switch(type)
	{
		case IT_HEALING:        return "Potion/Food";
		case IT_USABLE:         return "Usable";
		case IT_ETC:            return "Etc.";
		case IT_WEAPON:         return "Weapon";
		case IT_ARMOR:          return "Armor";
		case IT_CARD:           return "Card";
		case IT_PETEGG:         return "Pet Egg";
		case IT_PETARMOR:       return "Pet Accessory";
		case IT_AMMO:           return "Arrow/Ammunition";
		case IT_DELAYCONSUME:   return "Delay-Consume Usable";
		case IT_CASH:           return "Cash Usable";
	}
	return "Unknown Type";
}

/*==========================================
 * Converts the jobid from the format in itemdb 
 * to the format used by the map server. [Skotlex]
 *------------------------------------------*/
static void itemdb_jobid2mapid(unsigned int *bclass, unsigned int jobmask)
{
	int i;
	bclass[0]= bclass[1]= bclass[2]= 0;
	//Base classes
	if (jobmask & 1<<JOB_NOVICE)
	{	//Both Novice/Super-Novice are counted with the same ID
		bclass[0] |= 1<<MAPID_NOVICE;
		bclass[1] |= 1<<MAPID_NOVICE;
	}
	for (i = JOB_NOVICE+1; i <= JOB_THIEF; i++)
	{
		if (jobmask & 1<<i)
			bclass[0] |= 1<<(MAPID_NOVICE+i);
	}
	//2-1 classes
	if (jobmask & 1<<JOB_KNIGHT)
		bclass[1] |= 1<<MAPID_SWORDMAN;
	if (jobmask & 1<<JOB_PRIEST)
		bclass[1] |= 1<<MAPID_ACOLYTE;
	if (jobmask & 1<<JOB_WIZARD)
		bclass[1] |= 1<<MAPID_MAGE;
	if (jobmask & 1<<JOB_BLACKSMITH)
		bclass[1] |= 1<<MAPID_MERCHANT;
	if (jobmask & 1<<JOB_HUNTER)
		bclass[1] |= 1<<MAPID_ARCHER;
	if (jobmask & 1<<JOB_ASSASSIN)
		bclass[1] |= 1<<MAPID_THIEF;
	//2-2 classes
	if (jobmask & 1<<JOB_CRUSADER)
		bclass[2] |= 1<<MAPID_SWORDMAN;
	if (jobmask & 1<<JOB_MONK)
		bclass[2] |= 1<<MAPID_ACOLYTE;
	if (jobmask & 1<<JOB_SAGE)
		bclass[2] |= 1<<MAPID_MAGE;
	if (jobmask & 1<<JOB_ALCHEMIST)
		bclass[2] |= 1<<MAPID_MERCHANT;
	if (jobmask & 1<<JOB_BARD)
		bclass[2] |= 1<<MAPID_ARCHER;
//	Bard/Dancer share the same slot now.
//	if (jobmask & 1<<JOB_DANCER)
//		bclass[2] |= 1<<MAPID_ARCHER;
	if (jobmask & 1<<JOB_ROGUE)
		bclass[2] |= 1<<MAPID_THIEF;
	//Special classes that don't fit above.
	if (jobmask & 1<<21) //Taekwon boy
		bclass[0] |= 1<<MAPID_TAEKWON;
	if (jobmask & 1<<22) //Star Gladiator
		bclass[1] |= 1<<MAPID_TAEKWON;
	if (jobmask & 1<<23) //Soul Linker
		bclass[2] |= 1<<MAPID_TAEKWON;
	if (jobmask & 1<<JOB_GUNSLINGER)
		bclass[0] |= 1<<MAPID_GUNSLINGER;
	if (jobmask & 1<<JOB_NINJA)
		{bclass[0] |= 1<<MAPID_NINJA;
		bclass[1] |= 1<<MAPID_NINJA;}//Kagerou/Oboro jobs can equip Ninja equips. [Rytech]
	if (jobmask & 1<<26) //Bongun/Munak
		bclass[0] |= 1<<MAPID_GANGSI;
	if (jobmask & 1<<27) //Death Knight
		bclass[1] |= 1<<MAPID_GANGSI;
	if (jobmask & 1<<28) //Dark Collector
		bclass[2] |= 1<<MAPID_GANGSI;
	if (jobmask & 1<<29) //Kagerou / Oboro
		bclass[1] |= 1<<MAPID_NINJA;
}

static void create_dummy_data(void)
{
	memset(&dummy_item, 0, sizeof(struct item_data));
	dummy_item.nameid=500;
	dummy_item.weight=1;
	dummy_item.value_sell=1;
	dummy_item.type=IT_ETC; //Etc item
	safestrncpy(dummy_item.name,"UNKNOWN_ITEM",sizeof(dummy_item.name));
	safestrncpy(dummy_item.jname,"UNKNOWN_ITEM",sizeof(dummy_item.jname));
	dummy_item.view_id=UNKNOWN_ITEM_ID;
}

static struct item_data* create_item_data(int nameid)
{
	struct item_data *id;
	CREATE(id, struct item_data, 1);
	id->nameid = nameid;
	id->weight = 1;
	id->type = IT_ETC;
	return id;
}

/*==========================================
 * Loads (and creates if not found) an item from the db.
 *------------------------------------------*/
struct item_data* itemdb_load(int nameid) {
	struct item_data *id;

	if( nameid >= 0 && nameid < ARRAYLENGTH(itemdb_array) )
	{
		id = itemdb_array[nameid];
		if( id == NULL || id == &dummy_item )
			id = itemdb_array[nameid] = create_item_data(nameid);
		return id;
	}

	id = (struct item_data*)idb_get(itemdb_other, nameid);
	if( id == NULL || id == &dummy_item )
	{
		id = create_item_data(nameid);
		idb_put(itemdb_other, nameid, id);
	}
	return id;
}

/*==========================================
 * Loads an item from the db. If not found, it will return the dummy item.
 *------------------------------------------*/
struct item_data* itemdb_search(int nameid)
{
	struct item_data* id;
	if( nameid >= 0 && nameid < ARRAYLENGTH(itemdb_array) )
		id = itemdb_array[nameid];
	else
		id = (struct item_data*)idb_get(itemdb_other, nameid);

	if( id == NULL )
	{
		ShowWarning("itemdb_search: Item ID %d does not exists in the item_db. Using dummy data.\n", nameid);
		id = &dummy_item;
		dummy_item.nameid = nameid;
	}
	return id;
}

/*==========================================
 * Returns if given item is a player-equippable piece.
 *------------------------------------------*/
int itemdb_isequip(int nameid)
{
	int type=itemdb_type(nameid);
	switch (type) {
		case IT_WEAPON:
		case IT_ARMOR:
		case IT_AMMO:
			return 1;
		default:
			return 0;
	}
}

/*==========================================
 * Alternate version of itemdb_isequip
 *------------------------------------------*/
int itemdb_isequip2(struct item_data *data)
{ 
	nullpo_ret(data);
	switch(data->type) {
		case IT_WEAPON:
		case IT_ARMOR:
		case IT_AMMO:
			return 1;
		default:
			return 0;
	}
}

/*==========================================
 * Returns if given item's type is stackable.
 *------------------------------------------*/
int itemdb_isstackable(int nameid)
{
  int type=itemdb_type(nameid);
  switch(type) {
	  case IT_WEAPON:
	  case IT_ARMOR:
	  case IT_PETEGG:
	  case IT_PETARMOR:
		  return 0;
	  default:
		  return 1;
  }
}

/*==========================================
 * Alternate version of itemdb_isstackable
 *------------------------------------------*/
int itemdb_isstackable2(struct item_data *data)
{
  nullpo_ret(data);
  switch(data->type) {
	  case IT_WEAPON:
	  case IT_ARMOR:
	  case IT_PETEGG:
	  case IT_PETARMOR:
		  return 0;
	  default:
		  return 1;
  }
}


/*==========================================
 * Trade Restriction functions [Skotlex]
 *------------------------------------------*/
int itemdb_isdropable_sub(struct item_data *item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&1) || gmlv >= item->gm_lv_trade_override));
}

int itemdb_cantrade_sub(struct item_data* item, int gmlv, int gmlv2) {
	return (item && (!(item->flag.trade_restriction&2) || gmlv >= item->gm_lv_trade_override || gmlv2 >= item->gm_lv_trade_override));
}

int itemdb_canpartnertrade_sub(struct item_data* item, int gmlv, int gmlv2) {
	return (item && (item->flag.trade_restriction&4 || gmlv >= item->gm_lv_trade_override || gmlv2 >= item->gm_lv_trade_override));
}

int itemdb_cansell_sub(struct item_data* item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&8) || gmlv >= item->gm_lv_trade_override));
}

int itemdb_cancartstore_sub(struct item_data* item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&16) || gmlv >= item->gm_lv_trade_override));
}

int itemdb_canstore_sub(struct item_data* item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&32) || gmlv >= item->gm_lv_trade_override));
}

int itemdb_canguildstore_sub(struct item_data* item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&64) || gmlv >= item->gm_lv_trade_override));
}

int itemdb_canmail_sub(struct item_data* item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&128) || gmlv >= item->gm_lv_trade_override));
}

int itemdb_canauction_sub(struct item_data* item, int gmlv, int unused) {
	return (item && (!(item->flag.trade_restriction&256) || gmlv >= item->gm_lv_trade_override));
}

int itemdb_isrestricted(struct item* item, int gmlv, int gmlv2, int (*func)(struct item_data*, int, int))
{
	struct item_data* item_data = itemdb_search(item->nameid);
	int i;

	if (!func(item_data, gmlv, gmlv2))
		return 0;
	
	if(item_data->slot == 0 || itemdb_isspecial(item->card[0]))
		return 1;
	
	for(i = 0; i < item_data->slot; i++) {
		if (!item->card[i]) continue;
		if (!func(itemdb_search(item->card[i]), gmlv, gmlv2))
			return 0;
	}
	return 1;
}

/*==========================================
 *	Specifies if item-type should drop unidentified.
 *------------------------------------------*/
int itemdb_isidentified(int nameid) {
	int type=itemdb_type(nameid);
	switch (type) {
		case IT_WEAPON:
		case IT_ARMOR:
		case IT_PETARMOR:
			return 0;
		default:
			return 1;
	}
}
/* same as itemdb_isidentified but without a lookup */
int itemdb_isidentified2(struct item_data *data) {
	switch (data->type) {
		case IT_WEAPON:
		case IT_ARMOR:
		case IT_PETARMOR:
			return 0;
		default:
			return 1;
	}
}


/*==========================================
 * Search by name for the override flags available items
 * (Give item another sprite)
 *------------------------------------------*/
static bool itemdb_read_itemavail(char* str[], int columns, int current)
{// <nameid>,<sprite>
	int nameid, sprite;
	struct item_data *id;

	nameid = atoi(str[0]);

	if( ( id = itemdb->exists(nameid) ) == NULL )
	{
		ShowWarning("itemdb_read_itemavail: Invalid item id %d.\n", nameid);
		return false;
	}

	sprite = atoi(str[1]);

	if( sprite > 0 )
	{
		id->flag.available = 1;
		id->view_id = sprite;
	}
	else
	{
		id->flag.available = 0;
	}

	return true;
}

void itemdb_read_groups(void) {
	config_t item_group_conf;
	config_setting_t *itg = NULL, *it = NULL;
#ifdef RENEWAL
	const char *config_filename = "db/re/item_group.conf"; // FIXME hardcoded name
#else
	const char *config_filename = "db/pre-re/item_group.conf"; // FIXME hardcoded name
#endif
	const char *itname;
	int i = 0, count = 0, c;
	unsigned int *gsize = NULL;

	if (conf_read_file(&item_group_conf, config_filename)) {
		ShowError("can't read %s\n", config_filename);
		return;
	}
		
	gsize = aMalloc( config_setting_length(item_group_conf.root) * sizeof(unsigned int) );
	
	for(i = 0; i < config_setting_length(item_group_conf.root); i++)
		gsize[i] = 0;
	
	i = 0;
	while( (itg = config_setting_get_elem(item_group_conf.root,i++)) ) {
		const char *name = config_setting_name(itg);

		if( !itemdb->name2id(name) ) {
			ShowWarning("itemdb_read_groups: unknown group item '%s', skipping..\n",name);
			config_setting_remove(item_group_conf.root, name);
			--i;
			continue;
		}
		
		c = 0;
		while( (it = config_setting_get_elem(itg,c++)) ) {
			if( config_setting_is_list(it) )
				gsize[ i - 1 ] += config_setting_get_int_elem(it,1);
			else
				gsize[ i - 1 ] += 1;
		}
		
	}
		
	i = 0;
	CREATE(itemdb->groups, struct item_group, config_setting_length(item_group_conf.root));
	itemdb->group_count = (unsigned short)config_setting_length(item_group_conf.root);
	
	while( (itg = config_setting_get_elem(item_group_conf.root,i++)) ) {
		struct item_data *data = itemdb->name2id(config_setting_name(itg));
		int ecount = 0;
		
		data->group = &itemdb->groups[count];
		
		itemdb->groups[count].id = data->nameid;
		itemdb->groups[count].qty = gsize[ count ];

		CREATE(itemdb->groups[count].nameid, unsigned short, gsize[ count ] + 1);
		
		c = 0;
		while( (it = config_setting_get_elem(itg,c++)) ) {
			int repeat = 1;
			if( config_setting_is_list(it) ) {
				itname = config_setting_get_string_elem(it,0);
				repeat = config_setting_get_int_elem(it,1);
			} else
				itname = config_setting_get_string_elem(itg,c - 1);
			
			if( !( data = itemdb->name2id(itname) ) )
				ShowWarning("itemdb_read_groups: unknown item '%s' in group '%s'!\n",itname,config_setting_name(itg));
			
			itemdb->groups[count].nameid[ecount] = data ? data->nameid : 0;
			if( repeat > 1 ) {
				//memset would be better? I failed to get the following to work though hu
				//memset(&itemdb->groups[count].nameid[ecount+1],itemdb->groups[count].nameid[ecount],sizeof(itemdb->groups[count].nameid[0])*repeat);
				int z;
				for( z = ecount+1; z < ecount+repeat; z++ )
					itemdb->groups[count].nameid[z] = itemdb->groups[count].nameid[ecount];
			}
			ecount += repeat;
		}
		
		count++;
	}
		
	config_destroy(&item_group_conf);
	aFree(gsize);
	
	ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, config_filename);
}

void itemdb_read_packages(void) {
	config_t item_packages_conf;
	config_setting_t *itg = NULL, *it = NULL, *t = NULL;
#ifdef RENEWAL
	const char *config_filename = "db/re/item_packages.conf"; // FIXME hardcoded name
#else
	const char *config_filename = "db/pre-re/item_packages.conf"; // FIXME hardcoded name
#endif
	const char *itname;
	int i = 0, count = 0, c = 0;
	unsigned int *must = NULL, *random = NULL;
	
	if (conf_read_file(&item_packages_conf, config_filename)) {
		ShowError("can't read %s\n", config_filename);
		return;
	}
	
	must = aMalloc( config_setting_length(item_packages_conf.root) * sizeof(unsigned int) );
	random = aMalloc( config_setting_length(item_packages_conf.root) * sizeof(unsigned int) );

	for(i = 0; i < config_setting_length(item_packages_conf.root); i++) {
		must[i] = 0;
		random[i] = 0;
	}
	
	i = 0;
	while( (itg = config_setting_get_elem(item_packages_conf.root,i++)) ) {
		const char *name = config_setting_name(itg);
		
		if( !itemdb->name2id(name) ) {
			ShowWarning("itemdb_read_packages: unknown package item '%s', skipping..\n",name);
			config_setting_remove(item_packages_conf.root, name);
			--i;
			continue;
		}
		
		c = 0;
		while( (it = config_setting_get_elem(itg,c++)) ) {
			if( ( t = config_setting_get_member(it, "Random")) && !config_setting_get_bool(t) )
				must[ i - 1 ] += 1;
			else
				random[ i - 1 ] += 1;
		}
		
	}
	
	CREATE(itemdb->packages, struct item_package, config_setting_length(item_packages_conf.root));
	itemdb->package_count = (unsigned short)config_setting_length(item_packages_conf.root);
	
	i = 0;
	while( (itg = config_setting_get_elem(item_packages_conf.root,i++)) ) {
		struct item_data *data = itemdb->name2id(config_setting_name(itg));
		struct item_package_rand_entry *prev = NULL;
		int r = 0, m = 0;
		
		data->package = &itemdb->packages[count];
		
		itemdb->packages[count].id  = data->nameid;
		itemdb->packages[count].random_list = NULL;
		itemdb->packages[count].must_items = NULL;
		itemdb->packages[count].random_qty = random[ i - 1 ];
		itemdb->packages[count].must_qty = must[ i - 1 ];
		
		if( itemdb->packages[count].random_qty )
			CREATE(itemdb->packages[count].random_list, struct item_package_rand_entry, itemdb->packages[count].random_qty);
		if( itemdb->packages[count].must_qty )
			CREATE(itemdb->packages[count].must_items, struct item_package_must_entry, itemdb->packages[count].must_qty);
		
		c = 0;
		while( (it = config_setting_get_elem(itg,c++)) ) {
			int icount = 1, expire = 0, rate = 10000;
			bool announce = false, named = false;
			
			itname = config_setting_name(it);
			
			if( !( data = itemdb->name2id(itname) ) )
				ShowWarning("itemdb_read_packages: unknown item '%s' in package '%s'!\n",itname,config_setting_name(itg));

			if( ( t = config_setting_get_member(it, "Count")) )
				icount = config_setting_get_int(t);
			
			if( ( t = config_setting_get_member(it, "Expire")) )
				expire = config_setting_get_int(t);
			
			if( ( t = config_setting_get_member(it, "Rate")) ) {
				if( (rate = (unsigned short)config_setting_get_int(t)) > 10000 ) {
					ShowWarning("itemdb_read_packages: invalid rate (%d) for item '%s' in package '%s'!\n",itname,config_setting_name(itg));
					rate = 10000;
				}
			}

			if( ( t = config_setting_get_member(it, "Announce")) && config_setting_get_bool(t) )
				announce = true;

			if( ( t = config_setting_get_member(it, "Named")) && config_setting_get_bool(t) )
				named = true;
			
			if( ( t = config_setting_get_member(it, "Random")) && !config_setting_get_bool(t) ) {
				itemdb->packages[count].must_items[m].id = data ? data->nameid : 0;
				itemdb->packages[count].must_items[m].qty = icount;
				itemdb->packages[count].must_items[m].hours = expire;
				itemdb->packages[count].must_items[m].announce = announce == true ? 1 : 0;
				itemdb->packages[count].must_items[m].named = named == true ? 1 : 0;
				m++;
			} else {
				if( prev )
					prev->next = &itemdb->packages[count].random_list[r];
				
				itemdb->packages[count].random_list[r].id = data ? data->nameid : 0;
				itemdb->packages[count].random_list[r].qty = icount;
				if( (itemdb->packages[count].random_list[r].rate = rate) == 10000 ) {
					ShowWarning("itemdb_read_packages: item '%s' in '%s' has 100% drop rate!! set this item as 'Random: false' or other items won't drop!!!\n",itname,config_setting_name(itg));
				}
				itemdb->packages[count].random_list[r].hours = expire;
				itemdb->packages[count].random_list[r].announce = announce == true ? 1 : 0;
				itemdb->packages[count].random_list[r].named = named == true ? 1 : 0;
								
				prev = &itemdb->packages[count].random_list[r];
				
				r++;
			}
			
		}
		
		if( prev )
			prev->next = &itemdb->packages[count].random_list[0];
		
		if( itemdb->packages[count].random_qty == 1 ) {
			//item packages dont stop looping until something comes out of them, so if you have only one item in it the drop is guaranteed.
			ShowWarning("itemdb_read_packages: '%s' has only 1 random option, drop rate will be 100%!\n",itemdb_name(itemdb->packages[count].id));
			itemdb->packages[count].random_list[0].rate = 10000;
		}
		
		count++;
	}
	
	
	config_destroy(&item_packages_conf);
	aFree(must);
	aFree(random);
	
	ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, config_filename);
}

void itemdb_read_chains(void) {
	config_t item_chain_conf;
	config_setting_t *itc = NULL, *entry = NULL;
#ifdef RENEWAL
	const char *config_filename = "db/re/item_chain.conf"; // FIXME hardcoded name
#else
	const char *config_filename = "db/pre-re/item_chain.conf"; // FIXME hardcoded name
#endif
	int i = 0, count = 0;
	
	if (conf_read_file(&item_chain_conf, config_filename)) {
		ShowError("can't read %s\n", config_filename);
		return;
	}

	CREATE(itemdb->chains, struct item_chain, config_setting_length(item_chain_conf.root));
	itemdb->chain_count = (unsigned short)config_setting_length(item_chain_conf.root);
	
	while( (itc = config_setting_get_elem(item_chain_conf.root,i++)) ) {
		struct item_data *data = NULL;
		struct item_chain_entry *prev = NULL;
		const char *name = config_setting_name(itc);
		int c = 0;
		
		script->set_constant2(name,i-1,0);
		itemdb->chains[count].qty = (unsigned short)config_setting_length(itc);
		
		CREATE(itemdb->chains[count].items, struct item_chain_entry, config_setting_length(itc));
		
		while( (entry = config_setting_get_elem(itc,c++)) ) {
			const char *itname = config_setting_name(entry);
			if( itname[0] == 'I' && itname[1] == 'D' && strlen(itname) < 7 ) {
				if( !( data = itemdb->exists(atoi(itname+2)) ) )
					ShowWarning("itemdb_read_chains: unknown item ID '%d' in chain '%s'!\n",atoi(itname+2),name);
			} else if( !( data = itemdb->name2id(itname) ) )
				ShowWarning("itemdb_read_chains: unknown item '%s' in chain '%s'!\n",itname,name);
			
			if( prev )
				prev->next = &itemdb->chains[count].items[c - 1];
			
			itemdb->chains[count].items[c - 1].id = data ? data->nameid : 0;
			itemdb->chains[count].items[c - 1].rate = data ? config_setting_get_int(entry) : 0;
			
			prev = &itemdb->chains[count].items[c - 1];
		}
		
		if( prev )
			prev->next = &itemdb->chains[count].items[0];
		
		count++;
	}
	
	config_destroy(&item_chain_conf);
	
	if( !script->get_constant("ITMCHAIN_ORE",&i) )
		ShowWarning("itemdb_read_chains: failed to find 'ITMCHAIN_ORE' chain to link to cache!\n");
	else
		itemdb->chain_cache[ECC_ORE] = i;
	
	ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, config_filename);
}


/*==========================================
 * Reads item trade restrictions [Skotlex]
 *------------------------------------------*/
static bool itemdb_read_itemtrade(char* str[], int columns, int current)
{// <nameid>,<mask>,<gm level>
	int nameid, flag, gmlv;
	struct item_data *id;

	nameid = atoi(str[0]);

	if( ( id = itemdb->exists(nameid) ) == NULL )
	{
		//ShowWarning("itemdb_read_itemtrade: Invalid item id %d.\n", nameid);
		//return false;
		// FIXME: item_trade.txt contains items, which are commented in item database.
		return true;
	}

	flag = atoi(str[1]);
	gmlv = atoi(str[2]);

	if( flag < 0 || flag > 511 ) {//Check range
		ShowWarning("itemdb_read_itemtrade: Invalid trading mask %d for item id %d.\n", flag, nameid);
		return false;
	}
	if( gmlv < 1 )
	{
		ShowWarning("itemdb_read_itemtrade: Invalid override GM level %d for item id %d.\n", gmlv, nameid);
		return false;
	}

	id->flag.trade_restriction = flag;
	id->gm_lv_trade_override = gmlv;

	return true;
}

/*==========================================
 * Reads item delay amounts [Paradox924X]
 *------------------------------------------*/
static bool itemdb_read_itemdelay(char* str[], int columns, int current)
{// <nameid>,<delay>
	int nameid, delay;
	struct item_data *id;

	nameid = atoi(str[0]);

	if( ( id = itemdb->exists(nameid) ) == NULL )
	{
		ShowWarning("itemdb_read_itemdelay: Invalid item id %d.\n", nameid);
		return false;
	}

	delay = atoi(str[1]);

	if( delay < 0 )
	{
		ShowWarning("itemdb_read_itemdelay: Invalid delay %d for item id %d.\n", id->delay, nameid);
		return false;
	}

	id->delay = delay;

	return true;
}

/*==================================================================
 * Reads item stacking restrictions
 *----------------------------------------------------------------*/
static bool itemdb_read_stack(char* fields[], int columns, int current)
{// <item id>,<stack limit amount>,<type>
	unsigned short nameid, amount;
	unsigned int type;
	struct item_data* id;

	nameid = (unsigned short)strtoul(fields[0], NULL, 10);

	if( ( id = itemdb->exists(nameid) ) == NULL )
	{
		ShowWarning("itemdb_read_stack: Unknown item id '%hu'.\n", nameid);
		return false;
	}

	if( !itemdb_isstackable2(id) )
	{
		ShowWarning("itemdb_read_stack: Item id '%hu' is not stackable.\n", nameid);
		return false;
	}

	amount = (unsigned short)strtoul(fields[1], NULL, 10);
	type = strtoul(fields[2], NULL, 10);

	if( !amount )
	{// ignore
		return true;
	}

	id->stack.amount       = amount;
	id->stack.inventory    = (type&1)!=0;
	id->stack.cart         = (type&2)!=0;
	id->stack.storage      = (type&4)!=0;
	id->stack.guildstorage = (type&8)!=0;

	return true;
}


/// Reads items allowed to be sold in buying stores
static bool itemdb_read_buyingstore(char* fields[], int columns, int current)
{// <nameid>
	int nameid;
	struct item_data* id;

	nameid = atoi(fields[0]);

	if( ( id = itemdb->exists(nameid) ) == NULL )
	{
		ShowWarning("itemdb_read_buyingstore: Invalid item id %d.\n", nameid);
		return false;
	}

	if( !itemdb_isstackable2(id) )
	{
		ShowWarning("itemdb_read_buyingstore: Non-stackable item id %d cannot be enabled for buying store.\n", nameid);
		return false;
	}

	id->flag.buyingstore = true;

	return true;
}

/*******************************************
** Item usage restriction (item_nouse.txt)
********************************************/
static bool itemdb_read_nouse(char* fields[], int columns, int current)
{// <nameid>,<flag>,<override>
	int nameid, flag, override;
	struct item_data* id;

	nameid = atoi(fields[0]);
	
	if( ( id = itemdb->exists(nameid) ) == NULL ) {
		ShowWarning("itemdb_read_nouse: Invalid item id %d.\n", nameid);
		return false;
	}
	
	flag = atoi(fields[1]);
	override = atoi(fields[2]);

	id->item_usage.flag = flag;
	id->item_usage.override = override;

	return true;
}

/**
 * @return: amount of retrieved entries.
 **/
int itemdb_combo_split_atoi (char *str, int *val) {
	int i;
	
	for (i=0; i<MAX_ITEMS_PER_COMBO; i++) {
		if (!str) break;
		
		val[i] = atoi(str);
		
		str = strchr(str,':');
		
		if (str)
			*str++=0;
	}
	
	if( i == 0 ) //No data found.
		return 0;
	
	return i;
}
/**
 * <combo{:combo{:combo:{..}}}>,<{ script }>
 **/
void itemdb_read_combos() {
	uint32 lines = 0, count = 0;
	char line[1024];
	
	char path[256];
	FILE* fp;
	
	sprintf(path, "%s/%s", iMap->db_path, DBPATH"item_combo_db.txt");
	
	if ((fp = fopen(path, "r")) == NULL) {
		ShowError("itemdb_read_combos: File not found \"%s\".\n", path);
		return;
	}
	
	// process rows one by one
	while(fgets(line, sizeof(line), fp)) {
		char *str[2], *p;
		
		lines++;

		if (line[0] == '/' && line[1] == '/')
			continue;
		
		memset(str, 0, sizeof(str));
		
		p = line;
		
		p = trim(p);

		if (*p == '\0')
			continue;// empty line
		
		if (!strchr(p,','))
		{
			/* is there even a single column? */
			ShowError("itemdb_read_combos: Insufficient columns in line %d of \"%s\", skipping.\n", lines, path);
			continue;
		}
		
		str[0] = p;
		p = strchr(p,',');
		*p = '\0';
		p++;

		str[1] = p;
		p = strchr(p,',');		
		p++;
		
		if (str[1][0] != '{') {
			ShowError("itemdb_read_combos(#1): Invalid format (Script column) in line %d of \"%s\", skipping.\n", lines, path);
			continue;
		}
		
		/* no ending key anywhere (missing \}\) */
		if ( str[1][strlen(str[1])-1] != '}' ) {
			ShowError("itemdb_read_combos(#2): Invalid format (Script column) in line %d of \"%s\", skipping.\n", lines, path);
			continue;
		} else {
			int items[MAX_ITEMS_PER_COMBO];
			int v = 0, retcount = 0;
			struct item_data * id = NULL;
			int idx = 0;
			
			if((retcount = itemdb_combo_split_atoi(str[0], items)) < 2) {
				ShowError("itemdb_read_combos: line %d of \"%s\" doesn't have enough items to make for a combo (min:2), skipping.\n", lines, path);
				continue;
			}
			
			/* validate */
			for(v = 0; v < retcount; v++) {
				if( !itemdb->exists(items[v]) ) {
					ShowError("itemdb_read_combos: line %d of \"%s\" contains unknown item ID %d, skipping.\n", lines, path,items[v]);
					break;
				}
			}
			/* failed at some item */
			if( v < retcount )
				continue;

			id = itemdb->exists(items[0]);
			
			idx = id->combos_count;
			
			/* first entry, create */
			if( id->combos == NULL ) {
				CREATE(id->combos, struct item_combo*, 1);
				id->combos_count = 1;
			} else {
				RECREATE(id->combos, struct item_combo*, ++id->combos_count);
			}
			
			CREATE(id->combos[idx],struct item_combo,1);
			
			id->combos[idx]->nameid = aMalloc( retcount * sizeof(unsigned short) );
			id->combos[idx]->count = retcount;
			id->combos[idx]->script = parse_script(str[1], path, lines, 0);
			id->combos[idx]->id = count;
			id->combos[idx]->isRef = false;
			/* populate ->nameid field */
			for( v = 0; v < retcount; v++ ) {
				id->combos[idx]->nameid[v] = items[v];
			}
			
			/* populate the children to refer to this combo */
			for( v = 1; v < retcount; v++ ) {
				struct item_data * it;
				int index;
				
				it = itemdb->exists(items[v]);
				
				index = it->combos_count;
				
				if( it->combos == NULL ) {
					CREATE(it->combos, struct item_combo*, 1);
					it->combos_count = 1;
				} else {
					RECREATE(it->combos, struct item_combo*, ++it->combos_count);
				}
				
				CREATE(it->combos[index],struct item_combo,1);
				
				/* we copy previously alloc'd pointers and just set it to reference */
				memcpy(it->combos[index],id->combos[idx],sizeof(struct item_combo));
				/* we flag this way to ensure we don't double-dealloc same data */
				it->combos[index]->isRef = true;
			}
			
		}
		
		count++;
	}
	
	fclose(fp);
	
	ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"item_combo_db"CL_RESET"'.\n", count);
		
	return;
}



/*======================================
 * Applies gender restrictions according to settings. [Skotlex]
 *======================================*/
static int itemdb_gendercheck(struct item_data *id)
{
	if (id->nameid == WEDDING_RING_M) //Grom Ring
		return 1;
	if (id->nameid == WEDDING_RING_F) //Bride Ring
		return 0;
	if (id->look == W_MUSICAL && id->type == IT_WEAPON) //Musical instruments are always male-only
		return 1;
	if (id->look == W_WHIP && id->type == IT_WEAPON) //Whips are always female-only
		return 0;

	return (battle_config.ignore_items_gender) ? 2 : id->sex;
}
/**
 * [RRInd]
 * For backwards compatibility, in Renewal mode, MATK from weapons comes from the atk slot
 * We use a ':' delimiter which, if not found, assumes the weapon does not provide any matk.
 **/
void itemdb_re_split_atoi(char *str, int *atk, int *matk) {
	int i, val[2];

	for (i=0; i<2; i++) {
		if (!str) break;
		val[i] = atoi(str);
		str = strchr(str,':');
		if (str)
			*str++=0;
	}
	if( i == 0 ) {
		*atk = *matk = 0;
		return;//no data found
	}
	if( i == 1 ) {//Single Value, we assume it's the ATK
		*atk = val[0];
		*matk = 0;
		return;
	}
	//We assume we have 2 values.
	*atk = val[0];
	*matk = val[1];
	return;
}
/*==========================================
 * processes one itemdb entry
 *------------------------------------------*/
int itemdb_parse_dbrow(char** str, const char* source, int line, int scriptopt) {
	/*
		+----+--------------+---------------+------+-----------+------------+--------+--------+---------+-------+-------+------------+-------------+---------------+-----------------+--------------+-------------+------------+------+--------+--------------+----------------+
		| 00 |      01      |       02      |  03  |     04    |     05     |   06   |   07   |    08   |   09  |   10  |     11     |      12     |       13      |        14       |      15      |      16     |     17     |  18  |   19   |      20      |        21      |
		+----+--------------+---------------+------+-----------+------------+--------+--------+---------+-------+-------+------------+-------------+---------------+-----------------+--------------+-------------+------------+------+--------+--------------+----------------+
		| id | name_english | name_japanese | type | price_buy | price_sell | weight | attack | defence | range | slots | equip_jobs | equip_upper | equip_genders | equip_locations | weapon_level | equip_level | refineable | view | script | equip_script | unequip_script |
		+----+--------------+---------------+------+-----------+------------+--------+--------+---------+-------+-------+------------+-------------+---------------+-----------------+--------------+-------------+------------+------+--------+--------------+----------------+
	*/
	int nameid;
	struct item_data* id;
	unsigned char offset = 0;
	
	nameid = atoi(str[0]);
	if( nameid <= 0 ) {
		ShowWarning("itemdb_parse_dbrow: Invalid id %d in line %d of \"%s\", skipping.\n", nameid, line, source);
		return 0;
	}

	//ID,Name,Jname,Type,Price,Sell,Weight,ATK,DEF,Range,Slot,Job,Job Upper,Gender,Loc,wLV,eLV,refineable,View
	id = itemdb_load(nameid);
	safestrncpy(id->name, str[1], sizeof(id->name));
	safestrncpy(id->jname, str[2], sizeof(id->jname));

	id->type = atoi(str[3]);

	if( id->type < 0 || id->type == IT_UNKNOWN || id->type == IT_UNKNOWN2 || ( id->type > IT_DELAYCONSUME && id->type < IT_CASH ) || id->type >= IT_MAX )
	{// catch invalid item types
		ShowWarning("itemdb_parse_dbrow: Invalid item type %d for item %d. IT_ETC will be used.\n", id->type, nameid);
		id->type = IT_ETC;
	}

	if (id->type == IT_DELAYCONSUME)
	{	//Items that are consumed only after target confirmation
		id->type = IT_USABLE;
		id->flag.delay_consume = 1;
	} else //In case of an itemdb reload and the item type changed.
		id->flag.delay_consume = 0;

	//When a particular price is not given, we should base it off the other one
	//(it is important to make a distinction between 'no price' and 0z)
	if ( str[4][0] )
		id->value_buy = atoi(str[4]);
	else
		id->value_buy = atoi(str[5]) * 2;

	if ( str[5][0] )
		id->value_sell = atoi(str[5]);
	else
		id->value_sell = id->value_buy / 2;
	/* 
	if ( !str[4][0] && !str[5][0])
	{  
		ShowWarning("itemdb_parse_dbrow: No buying/selling price defined for item %d (%s), using 20/10z\n",       nameid, id->jname);
		id->value_buy = 20;
		id->value_sell = 10;
	} else
	*/
	if (id->value_buy/124. < id->value_sell/75.)
		ShowWarning("itemdb_parse_dbrow: Buying/Selling [%d/%d] price of item %d (%s) allows Zeny making exploit  through buying/selling at discounted/overcharged prices!\n",
			id->value_buy, id->value_sell, nameid, id->jname);

	id->weight = atoi(str[6]);
#ifdef RENEWAL
	if( iMap->db_use_sql_item_db ) {
		id->atk = atoi(str[7]);
		id->matk = atoi(str[8]);
		offset += 1;
	} else
		itemdb_re_split_atoi(str[7],&id->atk,&id->matk);
#else
	id->atk = atoi(str[7]);
#endif
	id->def = atoi(str[8+offset]);
	id->range = atoi(str[9+offset]);
	id->slot = atoi(str[10+offset]);

	if (id->slot > MAX_SLOTS) {
		ShowWarning("itemdb_parse_dbrow: Item %d (%s) specifies %d slots, but the server only supports up to %d. Using %d slots.\n", nameid, id->jname, id->slot, MAX_SLOTS, MAX_SLOTS);
		id->slot = MAX_SLOTS;
	}

	itemdb_jobid2mapid(id->class_base, (unsigned int)strtoul(str[11+offset],NULL,0));
	id->class_upper = atoi(str[12+offset]);
	id->sex	= atoi(str[13+offset]);
	id->equip = atoi(str[14+offset]);

	if (!id->equip && itemdb_isequip2(id)) {
		ShowWarning("Item %d (%s) is an equipment with no equip-field! Making it an etc item.\n", nameid, id->jname);
		id->type = IT_ETC;
	}

	id->wlv = cap_value(atoi(str[15+offset]), REFINE_TYPE_ARMOR, REFINE_TYPE_MAX);
#ifdef RENEWAL
	if( iMap->db_use_sql_item_db ) {
		id->elv = atoi(str[16+offset]);
		id->elvmax = atoi(str[17+offset]);
		offset += 1;
	} else
		itemdb_re_split_atoi(str[16],&id->elv,&id->elvmax);
#else
	id->elv = atoi(str[16]);
#endif
	id->flag.no_refine = atoi(str[17+offset]) ? 0 : 1; //FIXME: verify this
	id->look = atoi(str[18+offset]);

	id->flag.available = 1;
	id->view_id = 0;
	id->sex = itemdb_gendercheck(id); //Apply gender filtering.

	if (id->script) {
		script_free_code(id->script);
		id->script = NULL;
	}
	if (id->equip_script) {
		script_free_code(id->equip_script);
		id->equip_script = NULL;
	}
	if (id->unequip_script) {
		script_free_code(id->unequip_script);
		id->unequip_script = NULL;
	}

	if (*str[19+offset])
		id->script = parse_script(str[19+offset], source, line, scriptopt);
	if (*str[20+offset])
		id->equip_script = parse_script(str[20+offset], source, line, scriptopt);
	if (*str[21+offset])
		id->unequip_script = parse_script(str[21+offset], source, line, scriptopt);

	strdb_put(itemdb->names, id->name, id);
	return id->nameid;
}

/*==========================================
 * Reading item from item db
 * item_db2 overwriting item_db
 *------------------------------------------*/
static int itemdb_readdb(void)
{
	const char* filename[] = {
		DBPATH"item_db.txt",
		"item_db2.txt" };

	int fi;

	for( fi = 0; fi < ARRAYLENGTH(filename); ++fi ) {
		uint32 lines = 0, count = 0;
		char line[1024];

		char path[256];
		FILE* fp;

		sprintf(path, "%s/%s", iMap->db_path, filename[fi]);
		fp = fopen(path, "r");
		if( fp == NULL ) {
			ShowWarning("itemdb_readdb: File not found \"%s\", skipping.\n", path);
			continue;
		}

		// process rows one by one
		while(fgets(line, sizeof(line), fp))
		{
			char *str[32], *p;
			int i;
			lines++;
			if(line[0] == '/' && line[1] == '/')
				continue;
			memset(str, 0, sizeof(str));

			p = line;
			while( ISSPACE(*p) )
				++p;
			if( *p == '\0' )
				continue;// empty line
			for( i = 0; i < 19; ++i )
			{
				str[i] = p;
				p = strchr(p,',');
				if( p == NULL )
					break;// comma not found
				*p = '\0';
				++p;
			}

			if( p == NULL )
			{
				ShowError("itemdb_readdb: Insufficient columns in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}

			// Script
			if( *p != '{' )
			{
				ShowError("itemdb_readdb: Invalid format (Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			str[19] = p;
			p = strstr(p+1,"},");
			if( p == NULL )
			{
				ShowError("itemdb_readdb: Invalid format (Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			p[1] = '\0';
			p += 2;

			// OnEquip_Script
			if( *p != '{' )
			{
				ShowError("itemdb_readdb: Invalid format (OnEquip_Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			str[20] = p;
			p = strstr(p+1,"},");
			if( p == NULL )
			{
				ShowError("itemdb_readdb: Invalid format (OnEquip_Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			p[1] = '\0';
			p += 2;

			// OnUnequip_Script (last column)
			if( *p != '{' )
			{
				ShowError("itemdb_readdb: Invalid format (OnUnequip_Script column) in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
				continue;
			}
			str[21] = p;
			
			if ( str[21][strlen(str[21])-2] != '}' ) {
				/* lets count to ensure it's not something silly e.g. a extra space at line ending */
				int v, lcurly = 0, rcurly = 0;
				
				for( v = 0; v < strlen(str[21]); v++ ) {
					if( str[21][v] == '{' )
						lcurly++;
					else if ( str[21][v] == '}' )
						rcurly++;
				}
				
				if( lcurly != rcurly ) {
					ShowError("itemdb_readdb: Mismatching curly braces in line %d of \"%s\" (item with id %d), skipping.\n", lines, path, atoi(str[0]));
					continue;
				}
			}

			if (!itemdb->parse_dbrow(str, path, lines, 0))
				continue;
			
			count++;
		}

		fclose(fp);

		ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, filename[fi]);
	}

	return 0;
}
/*======================================
 * item_db table reading
 *======================================*/
static int itemdb_read_sqldb(void) {

	const char* item_db_name[] = {
								#ifdef RENEWAL
									iMap->item_db_re_db,
								#else
									iMap->item_db_db,
								#endif
									iMap->item_db2_db };
	int fi;
	
	for( fi = 0; fi < ARRAYLENGTH(item_db_name); ++fi ) {
		uint32 count = 0;

		// retrieve all rows from the item database
		if( SQL_ERROR == SQL->Query(mmysql_handle, "SELECT * FROM `%s`", item_db_name[fi]) ) {
			Sql_ShowDebug(mmysql_handle);
			continue;
		}

		// process rows one by one
		while( SQL_SUCCESS == SQL->NextRow(mmysql_handle) ) {// wrap the result into a TXT-compatible format
			char* str[ITEMDB_SQL_COLUMNS];
			char* dummy = "";
			int i;
			for( i = 0; i < ITEMDB_SQL_COLUMNS; ++i ) {
				SQL->GetData(mmysql_handle, i, &str[i], NULL);
				if( str[i] == NULL )
					str[i] = dummy; // get rid of NULL columns
			}

			if (!itemdb->parse_dbrow(str, item_db_name[fi], -(atoi(str[0])), SCRIPT_IGNORE_EXTERNAL_BRACKETS))
				continue;
			++count;
		}

		// free the query result
		SQL->FreeResult(mmysql_handle);

		ShowStatus("Done reading '"CL_WHITE"%lu"CL_RESET"' entries in '"CL_WHITE"%s"CL_RESET"'.\n", count, item_db_name[fi]);
	}

	return 0;
}

/*==========================================
* Unique item ID function
* Only one operation by once
* Flag:
* 0 return new id
* 1 set new value, checked with current value
* 2 set new value bypassing anything
* 3/other return last value
*------------------------------------------*/
uint64 itemdb_unique_id(int8 flag, int64 value) {
	static uint64 item_uid = 0;
	
	if(flag)
	{
		if(flag == 1)
		{	if(item_uid < value)
				return (item_uid = value);
		}else if(flag == 2)
			return (item_uid = value);
			
		return item_uid;
	}

	return ++item_uid;
}
int itemdb_uid_load() {

	char * uid;
	if (SQL_ERROR == SQL->Query(mmysql_handle, "SELECT `value` FROM `%s` WHERE `varname`='unique_id'",iMap->interreg_db))
		Sql_ShowDebug(mmysql_handle);

	if( SQL_SUCCESS != SQL->NextRow(mmysql_handle) ) {
		ShowError("itemdb_uid_load: Unable to fetch unique_id data\n");
		SQL->FreeResult(mmysql_handle);
		return -1;
	}

	SQL->GetData(mmysql_handle, 0, &uid, NULL);
	itemdb_unique_id(1, (uint64)strtoull(uid, NULL, 10));
	SQL->FreeResult(mmysql_handle);

	return 0;
}

/*====================================
 * read all item-related databases
 *------------------------------------*/
static void itemdb_read(void) {
	if (iMap->db_use_sql_item_db)
		itemdb_read_sqldb();
	else
		itemdb_readdb();
	
	itemdb_read_combos();
	itemdb->read_groups();
	itemdb->read_chains();
	itemdb->read_packages();

	sv->readdb(iMap->db_path, "item_avail.txt",         ',', 2, 2, -1, &itemdb_read_itemavail);
	sv->readdb(iMap->db_path, DBPATH"item_trade.txt",   ',', 3, 3, -1, &itemdb_read_itemtrade);
	sv->readdb(iMap->db_path, "item_delay.txt",         ',', 2, 2, -1, &itemdb_read_itemdelay);
	sv->readdb(iMap->db_path, "item_stack.txt",         ',', 3, 3, -1, &itemdb_read_stack);
	sv->readdb(iMap->db_path, DBPATH"item_buyingstore.txt",   ',', 1, 1, -1, &itemdb_read_buyingstore);
	sv->readdb(iMap->db_path, "item_nouse.txt",		 ',', 3, 3, -1, &itemdb_read_nouse);
	
	
	itemdb->name_constants();
	
	itemdb_uid_load();
}

/*==========================================
 * Initialize / Finalize
 *------------------------------------------*/

/// Destroys the item_data.
static void destroy_item_data(struct item_data* self, int free_self)
{
	if( self == NULL )
		return;
	// free scripts
	if( self->script )
		script_free_code(self->script);
	if( self->equip_script )
		script_free_code(self->equip_script);
	if( self->unequip_script )
		script_free_code(self->unequip_script);
	if( self->combos_count ) {
		int i;
		for( i = 0; i < self->combos_count; i++ ) {
			if( !self->combos[i]->isRef ) {
				aFree(self->combos[i]->nameid);
				script_free_code(self->combos[i]->script);
			}
			aFree(self->combos[i]);
		}
		aFree(self->combos);
	}
#if defined(DEBUG)
	// trash item
	memset(self, 0xDD, sizeof(struct item_data));
#endif
	// free self
	if( free_self )
		aFree(self);
}

/**
 * @see DBApply
 */
static int itemdb_final_sub(DBKey key, DBData *data, va_list ap)
{
	struct item_data *id = DB->data2ptr(data);

	if( id != &dummy_item )
		destroy_item_data(id, 1);

	return 0;
}

void itemdb_reload(void) {
	struct s_mapiterator* iter;
	struct map_session_data* sd;

	int i,d,k;
	
	// clear the previous itemdb data
	for( i = 0; i < ARRAYLENGTH(itemdb_array); ++i )
		if( itemdb_array[i] )
			destroy_item_data(itemdb_array[i], 1);

	for( i = 0; i < itemdb->group_count; i++ ) {
		if( itemdb->groups[i].nameid )
			aFree(itemdb->groups[i].nameid);
	}
	
	if( itemdb->groups )
		aFree(itemdb->groups);
	
	itemdb->groups = NULL;
	itemdb->group_count = 0;
	
	for( i = 0; i < itemdb->chain_count; i++ ) {
		if( itemdb->chains[i].items )
			aFree(itemdb->chains[i].items);
	}
	
	if( itemdb->chains )
		aFree(itemdb->chains);
	
	itemdb->chains = NULL;
	itemdb->chain_count = 0;
	
	for( i = 0; i < itemdb->package_count; i++ ) {
		if( itemdb->packages[i].random_list )
			aFree(itemdb->packages[i].random_list);
		if( itemdb->packages[i].must_items )
			aFree(itemdb->packages[i].must_items);
	}
	
	if( itemdb->packages )
		aFree(itemdb->packages);
	
	itemdb->packages = NULL;
	itemdb->package_count = 0;
	
	itemdb_other->clear(itemdb_other, itemdb_final_sub);
	
	memset(itemdb_array, 0, sizeof(itemdb_array));
	
	db_clear(itemdb->names);
		
	// read new data
	itemdb_read();
	
	//Epoque's awesome @reloaditemdb fix - thanks! [Ind]
	//- Fixes the need of a @reloadmobdb after a @reloaditemdb to re-link monster drop data
	for( i = 0; i < MAX_MOB_DB; i++ ) {
		struct mob_db *entry;
		if( !((i < 1324 || i > 1363) && (i < 1938 || i > 1946)) )
			continue;
		entry = mob_db(i);
		for(d = 0; d < MAX_MOB_DROP; d++) {
			struct item_data *id;
			if( !entry->dropitem[d].nameid )
				continue;
			id = itemdb_search(entry->dropitem[d].nameid);

			for (k = 0; k < MAX_SEARCH; k++) {
				if (id->mob[k].chance <= entry->dropitem[d].p)
					break;
			}

			if (k == MAX_SEARCH)
				continue;
			
			if (id->mob[k].id != i)
				memmove(&id->mob[k+1], &id->mob[k], (MAX_SEARCH-k-1)*sizeof(id->mob[0]));
			id->mob[k].chance = entry->dropitem[d].p;
			id->mob[k].id = i;
		}
	}

	// readjust itemdb pointer cache for each player
	iter = mapit_geteachpc();
	for( sd = (struct map_session_data*)mapit->first(iter); mapit->exists(iter); sd = (struct map_session_data*)mapit->next(iter) ) {
		memset(sd->item_delay, 0, sizeof(sd->item_delay));  // reset item delays
		pc->setinventorydata(sd);
		/* clear combo bonuses */
		if( sd->combos.count ) {
			aFree(sd->combos.bonus);
			aFree(sd->combos.id);
			sd->combos.bonus = NULL;
			sd->combos.id = NULL;
			sd->combos.count = 0;
			if( pc->load_combo(sd) > 0 )
				status_calc_pc(sd,0);
		}

	}
	mapit->free(iter);
}
void itemdb_name_constants(void) {
	DBIterator *iter = db_iterator(itemdb->names);
	struct item_data *data;
	
	for( data = dbi_first(iter); dbi_exists(iter); data = dbi_next(iter) )
		script->set_constant2(data->name,data->nameid,0);

	dbi_destroy(iter);	
}
void do_final_itemdb(void) {
	int i;

	for( i = 0; i < ARRAYLENGTH(itemdb_array); ++i )
		if( itemdb_array[i] )
			destroy_item_data(itemdb_array[i], 1);

	for( i = 0; i < itemdb->group_count; i++ ) {
		if( itemdb->groups[i].nameid )
			aFree(itemdb->groups[i].nameid);
	}
	
	if( itemdb->groups )
		aFree(itemdb->groups);

	for( i = 0; i < itemdb->chain_count; i++ ) {
		if( itemdb->chains[i].items )
			aFree(itemdb->chains[i].items);
	}
	
	if( itemdb->chains )
		aFree(itemdb->chains);
	
	for( i = 0; i < itemdb->package_count; i++ ) {
		if( itemdb->packages[i].random_list )
			aFree(itemdb->packages[i].random_list);
		if( itemdb->packages[i].must_items )
			aFree(itemdb->packages[i].must_items);
	}
	
	if( itemdb->packages )
		aFree(itemdb->packages);

	itemdb_other->destroy(itemdb_other, itemdb_final_sub);
	destroy_item_data(&dummy_item, 0);
	db_destroy(itemdb->names);
}

void do_init_itemdb(void) {
	memset(itemdb_array, 0, sizeof(itemdb_array));
	itemdb_other = idb_alloc(DB_OPT_BASE);
	itemdb->names = strdb_alloc(DB_OPT_BASE,ITEM_NAME_LENGTH);
	create_dummy_data(); //Dummy data item.
	itemdb_read();
	clif->cashshop_load();
}
/* incomplete */
void itemdb_defaults(void) {
	itemdb = &itemdb_s;
	
	itemdb->init = do_init_itemdb;
	itemdb->final = do_final_itemdb;
	itemdb->reload = itemdb_reload;//incomplete
	itemdb->name_constants = itemdb_name_constants;
	/* */
	itemdb->groups = NULL;
	itemdb->group_count = 0;
	/* */
	itemdb->chains = NULL;
	itemdb->chain_count = 0;
	/* */
	itemdb->packages = NULL;
	itemdb->package_count = 0;
	/* */
	itemdb->names = NULL;
	/* */
	itemdb->read_groups = itemdb_read_groups;
	itemdb->read_chains = itemdb_read_chains;
	itemdb->read_packages = itemdb_read_packages;
	/* */
	itemdb->search_name = itemdb_searchname;
	itemdb->search_name_array = itemdb_searchname_array;
	itemdb->load = itemdb_load;
	itemdb->search = itemdb_search;
	itemdb->parse_dbrow = itemdb_parse_dbrow;
	itemdb->exists = itemdb_exists;//incomplete
	itemdb->name2id = itemdb_name2id;
	itemdb->in_group = itemdb_in_group;
	itemdb->group_item = itemdb_searchrandomid;
	itemdb->chain_item = itemdb_chain_item;
	itemdb->package_item = itemdb_package_item;
}