/* NetHack 5.0	iactions.c	$NHDT-Date: 1781973051 2026/06/20 16:30:51 $  $NHDT-Branch: NetHack-5.0 $:$NHDT-Revision: 1.4 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Pasi Kallinen, 2026. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"

#ifdef NINTENDO_3DS
/* the 3DS front end overrides this menu's title (sys/3ds/3dsui.c) */
extern void _3ds_menu_title_override(const char *);
#endif

staticfn boolean item_naming_classification(struct obj *, char *, char *);
staticfn int item_reading_classification(struct obj *, char *);
staticfn void ia_addmenu(winid, int, char, const char *);
staticfn void itemactions_pushkeys(struct obj *, int);

enum item_action_actions {
    IA_NONE          = 0,
    IA_UNWIELD, /* hack for 'w-' */
    IA_APPLY_OBJ, /* 'a' */
    IA_DIP_OBJ, /* 'a' on a potion == dip */
    IA_NAME_OBJ, /* 'c' name individual item */
    IA_NAME_OTYP, /* 'C' name item's type */
    IA_DROP_OBJ, /* 'd' */
    IA_EAT_OBJ, /* 'e' */
    IA_ENGRAVE_OBJ, /* 'E' */
    IA_FIRE_OBJ, /* 'f' */
    IA_ADJUST_OBJ, /* 'i' #adjust inventory letter */
    IA_ADJUST_STACK, /* 'I' #adjust with count to split stack */
    IA_SACRIFICE, /* 'O' offer sacrifice */
    IA_BUY_OBJ, /* 'p' pay shopkeeper */
    IA_QUAFF_OBJ,
    IA_QUIVER_OBJ,
    IA_READ_OBJ,
    IA_RUB_OBJ,
    IA_THROW_OBJ,
    IA_TAKEOFF_OBJ,
    IA_TIP_CONTAINER,
    IA_INVOKE_OBJ,
    IA_WIELD_OBJ,
    IA_WEAR_OBJ,
    IA_SWAPWEAPON,
    IA_TWOWEAPON,
    IA_ZAP_OBJ,
    IA_WHATIS_OBJ, /* '/' specify inventory object */
    IA_LOOT_OBJ, /* 'l' loot a carried container (NetHack-3D parity) */
    IA_UNTRAP_OBJ, /* 'u' untrap with a tool (NetHack-3D parity) */
};

/* Actions offered by itemactions(), buffered so they can be presented
   with the item's primary use at the top instead of in command order. */
#define IA_MAX_ENTRIES 64
struct ia_entry {
    int act;
    char let;
    char txt[BUFSZ];
};
static struct ia_entry ia_entries[IA_MAX_ENTRIES];
static int ia_count = 0;

/* construct text for the menu entries for IA_NAME_OBJ and IA_NAME_OTYP */
staticfn boolean
item_naming_classification(
    struct obj *obj,
    char *onamebuf,
    char *ocallbuf)
{
    static const char
        Name[] = "Name",
        Rename[] = "Rename or un-name",
        Call[] = "Call",
        /* "re-call" seems a bit weird, but "recall" and
           "rename" don't fit for changing a type name */
        Recall[] = "Re-call or un-call";

    onamebuf[0] = ocallbuf[0] = '\0';
    if (name_ok(obj) == GETOBJ_SUGGEST) {
        Sprintf(onamebuf, "%s %s %s",
                (!has_oname(obj) || !*ONAME(obj)) ? Name : Rename,
                the_unique_obj(obj) ? "the"
                : !is_plural(obj) ? "this specific"
                  : "this stack of", /*"these",*/
                simpleonames(obj));
    }
    if (call_ok(obj) == GETOBJ_SUGGEST) {
        char *callname = simpleonames(obj);

        /* prefix known unique item with "the", make all other types plural */
        if (the_unique_obj(obj)) /* treats unID'd fake amulets as if real */
            callname = the(callname);
        else if (!is_plural(obj))
            callname = makeplural(callname);
        Sprintf(ocallbuf, "%s the type for %s",
                (!objects[obj->otyp].oc_uname
                 || !*objects[obj->otyp].oc_uname) ? Call : Recall,
                callname);
    }
    return (*onamebuf || *ocallbuf) ? TRUE : FALSE;
}

/* construct text for the menu entries for IA_READ_OBJ */
staticfn int
item_reading_classification(struct obj *obj, char *outbuf)
{
    int otyp = obj->otyp, res = IA_READ_OBJ;

    *outbuf = '\0';
    if (otyp == FORTUNE_COOKIE) {
        Strcpy(outbuf, "Read the message inside this cookie");
    } else if (otyp == T_SHIRT) {
        Strcpy(outbuf, "Read the slogan on the shirt");
    } else if (otyp == ALCHEMY_SMOCK) {
        Strcpy(outbuf, "Read the slogan on the apron");
    } else if (otyp == HAWAIIAN_SHIRT) {
        Strcpy(outbuf, "Look at the pattern on the shirt");
    } else if (obj->oclass == SCROLL_CLASS) {
        const char *magic = ((obj->dknown
#ifdef MAIL_STRUCTURES
                              && otyp != SCR_MAIL
#endif
                              && (otyp != SCR_BLANK_PAPER
                                  || !objects[otyp].oc_name_known))
                             ? " to activate its magic" : "");

        Sprintf(outbuf, "Read this scroll%s", magic);
    } else if (obj->oclass == SPBOOK_CLASS) {
        boolean novel = (otyp == SPE_NOVEL),
                blank = (otyp == SPE_BLANK_PAPER
                         && objects[otyp].oc_name_known),
                tome = (otyp == SPE_BOOK_OF_THE_DEAD
                        && objects[otyp].oc_name_known);

        Sprintf(outbuf, "%s this %s",
                (novel || blank) ? "Read" : tome ? "Examine" : "Study",
                novel ? simpleonames(obj) /* "novel" or "paperback book" */
                      : tome ? "tome" : "spellbook");
    } else {
        res = IA_NONE;
    }
    return res;
}

staticfn void
ia_addmenu(winid win, int act, char let, const char *txt)
{
    /* Entries are collected rather than added straight to the window:
       the menu is filled in alphabetical command order, which buries the
       one action the player most likely wants (read a scroll, quaff a
       potion, eat some food, ...) near the bottom of a long list.  The
       buffered list is sorted by relevance in ia_flush_menu() below. */
    if (ia_count < IA_MAX_ENTRIES) {
        ia_entries[ia_count].act = act;
        ia_entries[ia_count].let = let;
        Snprintf(ia_entries[ia_count].txt, BUFSZ, "%s", txt ? txt : "");
        ++ia_count;
    }
    (void) win;
}

/* how badly the player is likely to want this action for this item:
   0 = the item's primary use, 1 = some other use, 2 = bookkeeping */
staticfn int
ia_priority(struct obj *otmp, int act)
{
    int primary = IA_NONE;

    switch (otmp->oclass) {
    case SCROLL_CLASS:
    case SPBOOK_CLASS:
        primary = IA_READ_OBJ;
        break;
    case POTION_CLASS:
        primary = IA_QUAFF_OBJ;
        break;
    case WAND_CLASS:
        primary = IA_ZAP_OBJ;
        break;
    case FOOD_CLASS:
        primary = IA_EAT_OBJ;
        break;
    case WEAPON_CLASS:
        primary = IA_WIELD_OBJ;
        break;
    case ARMOR_CLASS:
    case RING_CLASS:
    case AMULET_CLASS:
        primary = (otmp->owornmask & (W_ARMOR | W_ACCESSORY)) != 0
                      ? IA_TAKEOFF_OBJ : IA_WEAR_OBJ;
        break;
    default:
        primary = IA_APPLY_OBJ;
        break;
    }
    if (act == primary)
        return 0;
    switch (act) {
    case IA_APPLY_OBJ:
    case IA_READ_OBJ:
    case IA_QUAFF_OBJ:
    case IA_EAT_OBJ:
    case IA_WIELD_OBJ:
    case IA_WEAR_OBJ:
    case IA_TAKEOFF_OBJ:
    case IA_ZAP_OBJ:
    case IA_TIP_CONTAINER:
    case IA_DIP_OBJ:
    case IA_INVOKE_OBJ:
    case IA_RUB_OBJ:
    case IA_FIRE_OBJ:
    case IA_THROW_OBJ:
        return 1;
    default:
        return 2;
    }
}

/* Add the collected entries, most relevant first, keeping the original
   relative order within each group. */
staticfn void
ia_flush_menu(winid win, struct obj *otmp)
{
    int prio, i;

    for (prio = 0; prio <= 2; prio++)
        for (i = 0; i < ia_count; i++)
            if (ia_priority(otmp, ia_entries[i].act) == prio) {
                anything any;
                int clr = NO_COLOR;

                any = cg.zeroany;
                any.a_int = ia_entries[i].act;
                add_menu(win, &nul_glyphinfo, &any, ia_entries[i].let, 0,
                         ATR_NONE, clr, ia_entries[i].txt,
                         MENU_ITEMFLAGS_NONE);
            }
    ia_count = 0;
}

/* set up a command to execute on a specific item next */
staticfn void
itemactions_pushkeys(struct obj *otmp, int act)
{
    switch (act) {
    default:
        impossible("Unknown item action %d", act);
        break;
    case IA_NONE:
        break;
    case IA_UNWIELD:
        cmdq_add_ec(CQ_CANNED, (otmp == uwep) ? dowield
                    : (otmp == uswapwep) ? remarm_swapwep
                      : (otmp == uquiver) ? dowieldquiver
                        : donull); /* can't happen */
        cmdq_add_key(CQ_CANNED, HANDS_SYM);
        break;
    case IA_APPLY_OBJ:
        cmdq_add_ec(CQ_CANNED, doapply);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_DIP_OBJ:
        /* #altdip instead of normal #dip - takes potion to dip into
           first (the inventory item instigating this) and item to
           be dipped second, also ignores floor features such as
           fountain/sink so we don't need to force m-prefix here */
        cmdq_add_ec(CQ_CANNED, dip_into);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_NAME_OBJ:
    case IA_NAME_OTYP:
        cmdq_add_ec(CQ_CANNED, docallcmd);
        cmdq_add_key(CQ_CANNED, (act == IA_NAME_OBJ) ? 'i' : 'o');
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_DROP_OBJ:
        cmdq_add_ec(CQ_CANNED, dodrop);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_EAT_OBJ:
        /* start with m-prefix; for #eat, it means ignore floor food
           if present and eat food from invent */
        cmdq_add_ec(CQ_CANNED, do_reqmenu);
        cmdq_add_ec(CQ_CANNED, doeat);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_ENGRAVE_OBJ:
        cmdq_add_ec(CQ_CANNED, doengrave);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_FIRE_OBJ:
        cmdq_add_ec(CQ_CANNED, dofire);
        break;
    case IA_ADJUST_OBJ:
        cmdq_add_ec(CQ_CANNED, doorganize); /* #adjust */
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_ADJUST_STACK:
        cmdq_add_ec(CQ_CANNED, adjust_split); /* #altadjust */
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_SACRIFICE:
        cmdq_add_ec(CQ_CANNED, dosacrifice);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_BUY_OBJ:
        cmdq_add_ec(CQ_CANNED, dopay);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_QUAFF_OBJ:
        /* start with m-prefix; for #quaff, it means ignore fountain
           or sink if present and drink a potion from invent */
        cmdq_add_ec(CQ_CANNED, do_reqmenu);
        cmdq_add_ec(CQ_CANNED, dodrink);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_QUIVER_OBJ:
        cmdq_add_ec(CQ_CANNED, dowieldquiver);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_READ_OBJ:
        cmdq_add_ec(CQ_CANNED, doread);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_RUB_OBJ:
        cmdq_add_ec(CQ_CANNED, dorub);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_THROW_OBJ:
        cmdq_add_ec(CQ_CANNED, dothrow);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_TAKEOFF_OBJ:
        cmdq_add_ec(CQ_CANNED, ia_dotakeoff); /* #altdotakeoff */
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_TIP_CONTAINER:
        /* start with m-prefix to skip floor containers;
           for menustyle:Traditional when more than one floor container
           is present, player will get a #tip menu and have to pick
           the "tip something being carried" choice, then this item
           will be already chosen from inventory; suboptimal but
           possibly an acceptable tradeoff since combining item actions
           with use of traditional ggetobj() is an unlikely scenario */
        cmdq_add_ec(CQ_CANNED, do_reqmenu);
        cmdq_add_ec(CQ_CANNED, dotip);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_INVOKE_OBJ:
        cmdq_add_ec(CQ_CANNED, doinvoke);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_WIELD_OBJ:
        cmdq_add_ec(CQ_CANNED, dowield);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_WEAR_OBJ:
        cmdq_add_ec(CQ_CANNED, dowear);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_SWAPWEAPON:
        cmdq_add_ec(CQ_CANNED, doswapweapon);
        break;
    case IA_TWOWEAPON:
        cmdq_add_ec(CQ_CANNED, dotwoweapon);
        break;
    case IA_ZAP_OBJ:
        cmdq_add_ec(CQ_CANNED, dozap);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_WHATIS_OBJ:
        cmdq_add_ec(CQ_CANNED, dowhatis); /* "/" command */
        cmdq_add_key(CQ_CANNED, 'i');     /* "i" == item from inventory */
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_LOOT_OBJ:
        cmdq_add_ec(CQ_CANNED, doloot);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    case IA_UNTRAP_OBJ:
        cmdq_add_ec(CQ_CANNED, dountrap);
        cmdq_add_key(CQ_CANNED, otmp->invlet);
        break;
    }
}

#ifdef NINTENDO_3DS
/* ---- NetHack-3D inventory action parity --------------------------------
   The example port (example/src/ui/app/inventory/actions.ts) builds the
   per-item action menu from a fixed list of actions, filtered by the item's
   inventory category and a handful of special support rules.  The 3DS port
   uses the same list so both ports offer identical actions for every item.
   --------------------------------------------------------------------- */

enum ia_ex_id {
    IAEX_APPLY,
    IAEX_INVOKE,
    IAEX_TIP,
    IAEX_LOOT,
    IAEX_DROP,
    IAEX_PAY,
    IAEX_EAT,
    IAEX_QUAFF,
    IAEX_READ,
    IAEX_RUB,
    IAEX_THROW,
    IAEX_WIELD,
    IAEX_QUIVER,
    IAEX_WEAR,
    IAEX_TAKEOFF,
    IAEX_PUTON,
    IAEX_REMOVE,
    IAEX_ZAP,
    IAEX_UNTRAP,
    IAEX_OFFER,
    IAEX_NAME,
    IAEX_CALL,
    IAEX_ADJUST,
    IAEX_ENGRAVE,
    IAEX_DIP,
    IAEX_INFO,
    IAEX_COUNT
};

#define IAEX_BIT(id) (1UL << (id))

/* inventory category of an object, matching classifyInventoryCategory() in
   the example port; -1 means "unknown / mixed" (permissive) */
static int
ia_category_for_class(int oclass)
{
    switch (oclass) {
    case WEAPON_CLASS:
        return 0; /* weapons */
    case ARMOR_CLASS:
        return 1; /* armor */
    case RING_CLASS:
        return 2; /* rings */
    case AMULET_CLASS:
        return 3; /* amulets */
    case TOOL_CLASS:
        return 4; /* tools */
    case FOOD_CLASS:
        return 5; /* comestibles */
    case POTION_CLASS:
        return 6; /* potions */
    case SCROLL_CLASS:
        return 7; /* scrolls */
    case SPBOOK_CLASS:
        return 8; /* spellbooks */
    case WAND_CLASS:
        return 9; /* wands */
    case COIN_CLASS:
        return 10; /* coins */
    case GEM_CLASS:
        return 11; /* gems/stones */
    case ROCK_CLASS:
        return 12; /* boulders/statues */
    case BALL_CLASS:
        return 13; /* iron balls */
    case CHAIN_CLASS:
        return 14; /* chains */
    case VENOM_CLASS:
        return 15; /* venoms */
    default:
        return -1;
    }
}

/* actions blocked for each category (inventoryCategoryActionBlocklist);
   index order matches ia_category_for_class(), with unknown/mixed items
   (bagged/boxed, -1) being permissive */
static const unsigned long ia_category_blocklist[16] = {
    /* weapons */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_QUAFF)
        | IAEX_BIT(IAEX_READ) | IAEX_BIT(IAEX_WEAR)
        | IAEX_BIT(IAEX_TAKEOFF) | IAEX_BIT(IAEX_PUTON)
        | IAEX_BIT(IAEX_REMOVE) | IAEX_BIT(IAEX_ZAP),
    /* armor */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_QUAFF)
        | IAEX_BIT(IAEX_READ) | IAEX_BIT(IAEX_WIELD)
        | IAEX_BIT(IAEX_PUTON) | IAEX_BIT(IAEX_REMOVE)
        | IAEX_BIT(IAEX_ZAP) | IAEX_BIT(IAEX_ENGRAVE),
    /* rings */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_QUAFF)
        | IAEX_BIT(IAEX_READ) | IAEX_BIT(IAEX_WIELD)
        | IAEX_BIT(IAEX_WEAR) | IAEX_BIT(IAEX_TAKEOFF)
        | IAEX_BIT(IAEX_ZAP),
    /* amulets */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_QUAFF)
        | IAEX_BIT(IAEX_READ) | IAEX_BIT(IAEX_WIELD)
        | IAEX_BIT(IAEX_WEAR) | IAEX_BIT(IAEX_TAKEOFF)
        | IAEX_BIT(IAEX_ZAP),
    /* tools */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_QUAFF)
        | IAEX_BIT(IAEX_READ) | IAEX_BIT(IAEX_WEAR)
        | IAEX_BIT(IAEX_TAKEOFF) | IAEX_BIT(IAEX_PUTON)
        | IAEX_BIT(IAEX_REMOVE) | IAEX_BIT(IAEX_ZAP),
    /* comestibles */ IAEX_BIT(IAEX_QUAFF) | IAEX_BIT(IAEX_READ)
        | IAEX_BIT(IAEX_WIELD) | IAEX_BIT(IAEX_WEAR)
        | IAEX_BIT(IAEX_TAKEOFF) | IAEX_BIT(IAEX_PUTON)
        | IAEX_BIT(IAEX_REMOVE) | IAEX_BIT(IAEX_ZAP)
        | IAEX_BIT(IAEX_ENGRAVE),
    /* potions */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_READ)
        | IAEX_BIT(IAEX_WEAR) | IAEX_BIT(IAEX_TAKEOFF)
        | IAEX_BIT(IAEX_PUTON) | IAEX_BIT(IAEX_REMOVE)
        | IAEX_BIT(IAEX_ZAP) | IAEX_BIT(IAEX_ENGRAVE),
    /* scrolls */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_QUAFF)
        | IAEX_BIT(IAEX_WIELD) | IAEX_BIT(IAEX_WEAR)
        | IAEX_BIT(IAEX_TAKEOFF) | IAEX_BIT(IAEX_PUTON)
        | IAEX_BIT(IAEX_REMOVE) | IAEX_BIT(IAEX_ZAP)
        | IAEX_BIT(IAEX_ENGRAVE),
    /* spellbooks */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_QUAFF)
        | IAEX_BIT(IAEX_WIELD) | IAEX_BIT(IAEX_WEAR)
        | IAEX_BIT(IAEX_TAKEOFF) | IAEX_BIT(IAEX_PUTON)
        | IAEX_BIT(IAEX_REMOVE) | IAEX_BIT(IAEX_ZAP)
        | IAEX_BIT(IAEX_ENGRAVE),
    /* wands */ IAEX_BIT(IAEX_QUAFF) | IAEX_BIT(IAEX_WIELD)
        | IAEX_BIT(IAEX_WEAR) | IAEX_BIT(IAEX_TAKEOFF)
        | IAEX_BIT(IAEX_PUTON) | IAEX_BIT(IAEX_REMOVE),
    /* coins */ IAEX_BIT(IAEX_QUAFF) | IAEX_BIT(IAEX_READ)
        | IAEX_BIT(IAEX_WIELD) | IAEX_BIT(IAEX_WEAR)
        | IAEX_BIT(IAEX_TAKEOFF) | IAEX_BIT(IAEX_PUTON)
        | IAEX_BIT(IAEX_REMOVE) | IAEX_BIT(IAEX_ZAP)
        | IAEX_BIT(IAEX_ENGRAVE) | IAEX_BIT(IAEX_DIP),
    /* gems_stones */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_QUAFF)
        | IAEX_BIT(IAEX_READ) | IAEX_BIT(IAEX_WIELD)
        | IAEX_BIT(IAEX_WEAR) | IAEX_BIT(IAEX_TAKEOFF)
        | IAEX_BIT(IAEX_PUTON) | IAEX_BIT(IAEX_REMOVE)
        | IAEX_BIT(IAEX_ZAP),
    /* boulders_statues */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_QUAFF)
        | IAEX_BIT(IAEX_WIELD) | IAEX_BIT(IAEX_WEAR)
        | IAEX_BIT(IAEX_TAKEOFF) | IAEX_BIT(IAEX_PUTON)
        | IAEX_BIT(IAEX_REMOVE) | IAEX_BIT(IAEX_ZAP)
        | IAEX_BIT(IAEX_ENGRAVE),
    /* iron_balls */ IAEX_BIT(IAEX_EAT) | IAEX_BIT(IAEX_QUAFF)
        | IAEX_BIT(IAEX_READ) | IAEX_BIT(IAEX_WIELD)
        | IAEX_BIT(IAEX_WEAR) | IAEX_BIT(IAEX_TAKEOFF)
        | IAEX_BIT(IAEX_PUTON) | IAEX_BIT(IAEX_REMOVE)
        | IAEX_BIT(IAEX_ZAP) | IAEX_BIT(IAEX_ENGRAVE),
    /* chains */ IAEX_BIT(IAEX_QUAFF) | IAEX_BIT(IAEX_READ)
        | IAEX_BIT(IAEX_WIELD) | IAEX_BIT(IAEX_WEAR)
        | IAEX_BIT(IAEX_TAKEOFF) | IAEX_BIT(IAEX_PUTON)
        | IAEX_BIT(IAEX_REMOVE) | IAEX_BIT(IAEX_ZAP)
        | IAEX_BIT(IAEX_ENGRAVE),
    /* venoms */ IAEX_BIT(IAEX_WEAR) | IAEX_BIT(IAEX_TAKEOFF)
        | IAEX_BIT(IAEX_PUTON) | IAEX_BIT(IAEX_REMOVE)
        | IAEX_BIT(IAEX_ZAP),
};

/* special item-support rules from the example port */
static boolean
ia_support_rub(struct obj *otmp)
{
    switch (otmp->otyp) {
    case OIL_LAMP:
    case MAGIC_LAMP:
    case BRASS_LANTERN:
        return TRUE;
    default:
        break;
    }
    if (otmp->oclass == GEM_CLASS)
        return is_graystone(otmp);
    return FALSE;
}

static boolean
ia_support_tip(struct obj *otmp)
{
    return otmp->otyp == HORN_OF_PLENTY || Is_container(otmp);
}

static boolean
ia_support_invoke(struct obj *otmp)
{
    switch (otmp->otyp) {
    case CRYSTAL_BALL:
    case MAGIC_LAMP:
    case OIL_LAMP:
    case BRASS_LANTERN:
    case MIRROR:
    case BELL_OF_OPENING:
    case CANDELABRUM_OF_INVOCATION:
    case SPE_BOOK_OF_THE_DEAD:
    case AMULET_OF_YENDOR:
    case FAKE_AMULET_OF_YENDOR:
        return TRUE;
    default:
        break;
    }
    switch (otmp->oclass) {
    case WEAPON_CLASS:
    case ARMOR_CLASS:
    case RING_CLASS:
    case AMULET_CLASS:
    case TOOL_CLASS:
    case SPBOOK_CLASS:
        return TRUE;
    default:
        return FALSE;
    }
}

static boolean
ia_support_call(int cat)
{
    switch (cat) {
    case 1: /* armor */
    case 2: /* rings */
    case 3: /* amulets */
    case 4: /* tools */
    case 6: /* potions */
    case 7: /* scrolls */
    case 8: /* spellbooks */
    case 9: /* wands */
    case 11: /* gems/stones */
        return TRUE;
    default:
        return FALSE;
    }
}

/* semantic applicability checks: an action is offered only when it is
   meaningful for the object, on top of the category blocklist */
static boolean
ia_support_read(struct obj *otmp)
{
    switch (otmp->oclass) {
    case SCROLL_CLASS:
    case SPBOOK_CLASS:
        return TRUE;
    default:
        break;
    }
    switch (otmp->otyp) {
    case FORTUNE_COOKIE:
    case T_SHIRT:
    case ALCHEMY_SMOCK:
    case HAWAIIAN_SHIRT:
        return TRUE;
    default:
        return FALSE;
    }
}

static boolean
ia_support_engrave(struct obj *otmp)
{
    switch (otmp->oclass) {
    case WEAPON_CLASS:
    case WAND_CLASS:
    case GEM_CLASS:
    case RING_CLASS:
        return TRUE;
    default:
        break;
    }
    return otmp->otyp == MAGIC_MARKER || otmp->otyp == TOWEL;
}

static boolean
ia_support_wield(struct obj *otmp)
{
    return (otmp->oclass == WEAPON_CLASS
            || is_weptool(otmp)
            || is_wet_towel(otmp)
            || otmp->otyp == HEAVY_IRON_BALL
            || otmp->otyp == TIN_OPENER);
}

static void
ia_ex_addmenu(winid win, int act, char let, const char *txt)
{
    anything any;

    any = cg.zeroany;
    any.a_int = act;
    add_menu(win, &nul_glyphinfo, &any, let, 0, ATR_NONE, NO_COLOR,
             txt, MENU_ITEMFLAGS_NONE);
}

/* one collected menu entry, ready to be emitted */
struct ia_ex_entry {
    int id;
    int act;
    char let;
    const char *txt;
};
static struct ia_ex_entry ia_ex_ent[IAEX_COUNT];
static int ia_ex_nent = 0;

static void
ia_ex_push(int id, int act, char let, const char *txt)
{
    if (ia_ex_nent < IAEX_COUNT) {
        ia_ex_ent[ia_ex_nent].id = id;
        ia_ex_ent[ia_ex_nent].act = act;
        ia_ex_ent[ia_ex_nent].let = let;
        ia_ex_ent[ia_ex_nent].txt = txt;
        ia_ex_nent++;
    }
}

static void
ia_ex_emit(winid win, int prim)
{
    int i, m[IAEX_COUNT] = { 0 };

    /* the item's primary use comes first, then every other action in its
       normal list order */
    for (i = 0; i < ia_ex_nent; i++)
        if (ia_ex_ent[i].id == prim) {
            ia_ex_addmenu(win, ia_ex_ent[i].act, ia_ex_ent[i].let,
                          ia_ex_ent[i].txt);
            m[i] = 1;
            break;
        }
    for (i = 0; i < ia_ex_nent; i++)
        if (!m[i])
            ia_ex_addmenu(win, ia_ex_ent[i].act, ia_ex_ent[i].let,
                          ia_ex_ent[i].txt);
}

/* the single most relevant action for an item, moved to the top of the
   action menu (potion -> quaff, food -> eat, scroll/spellbook -> read,
   wand -> zap, weapon -> wield/unwield, armor -> wear/take off,
   ring/amulet -> put on/remove, everything else -> apply) */
static int
ia_primary_id(struct obj *otmp)
{
    boolean worn = (otmp->owornmask & (W_ARMOR | W_ACCESSORY)) != 0;

    switch (otmp->oclass) {
    case SCROLL_CLASS:
    case SPBOOK_CLASS:
        return IAEX_READ;
    case POTION_CLASS:
        return IAEX_QUAFF;
    case WAND_CLASS:
        return IAEX_ZAP;
    case FOOD_CLASS:
        return IAEX_EAT;
    case WEAPON_CLASS:
        return IAEX_WIELD;
    case ARMOR_CLASS:
        return worn ? IAEX_TAKEOFF : IAEX_WEAR;
    case RING_CLASS:
    case AMULET_CLASS:
        return worn ? IAEX_REMOVE : IAEX_PUTON;
    default:
        return IAEX_APPLY;
    }
}

/* build the NetHack-3D item action menu for otmp */
static void
ia_example_menu(winid win, struct obj *otmp)
{
    int cat = ia_category_for_class(otmp->oclass);
    unsigned long blocked = (cat >= 0) ? ia_category_blocklist[cat] : 0UL;
    boolean worn = (otmp->owornmask & (W_ARMOR | W_ACCESSORY)) != 0;
    boolean wielded = (otmp == uwep || otmp == uswapwep
                       || otmp == uquiver);
    boolean potion = (otmp->oclass == POTION_CLASS);
    boolean weapon = (otmp->oclass == WEAPON_CLASS);
    struct monst *mtmp;
    int i;

    ia_ex_nent = 0;
    for (i = 0; i < IAEX_COUNT; i++) {
        if (blocked & IAEX_BIT(i))
            continue;
        switch (i) {
        case IAEX_APPLY:
            /* apply is offered for every item; the game itself reports
               when an object cannot be applied */
            ia_ex_push(i, IA_APPLY_OBJ, 'a', "Apply");
            break;
        case IAEX_INVOKE:
            if (ia_support_invoke(otmp))
                ia_ex_push(i, IA_INVOKE_OBJ, 'V', "Invoke");
            break;
        case IAEX_TIP:
            if (ia_support_tip(otmp))
                ia_ex_push(i, IA_TIP_CONTAINER, 'T', "Tip");
            break;
        case IAEX_LOOT:
            if (Is_container(otmp))
                ia_ex_push(i, IA_LOOT_OBJ, 'l', "Loot");
            break;
        case IAEX_DROP:
            if (!worn)
                ia_ex_push(i, IA_DROP_OBJ, 'd', "Drop");
            break;
        case IAEX_PAY:
            if (otmp->unpaid
                && (mtmp = shop_keeper(*in_rooms(u.ux, u.uy, SHOPBASE)))
                   != 0
                && inhishop(mtmp))
                ia_ex_push(i, IA_BUY_OBJ, 'p', "Pay");
            break;
        case IAEX_EAT:
            if (is_edible(otmp))
                ia_ex_push(i, IA_EAT_OBJ, 'e', "Eat");
            break;
        case IAEX_QUAFF:
            if (potion)
                ia_ex_push(i, IA_QUAFF_OBJ, 'q', "Quaff");
            break;
        case IAEX_READ:
            if (ia_support_read(otmp))
                ia_ex_push(i, IA_READ_OBJ, 'r', "Read");
            break;
        case IAEX_RUB:
            if (ia_support_rub(otmp))
                ia_ex_push(i, IA_RUB_OBJ, 'R', "Rub");
            break;
        case IAEX_THROW:
            if (!worn)
                ia_ex_push(i, IA_THROW_OBJ, 't', "Throw");
            break;
        case IAEX_WIELD:
            if (wielded)
                ia_ex_push(i, IA_UNWIELD, '-', "Unwield");
            else if (ia_support_wield(otmp))
                ia_ex_push(i, IA_WIELD_OBJ, 'w', "Wield");
            break;
        case IAEX_QUIVER:
            if (weapon)
                ia_ex_push(i, IA_QUIVER_OBJ, 'Q', "Quiver");
            break;
        case IAEX_WEAR:
            if (otmp->oclass == ARMOR_CLASS
                && !(otmp->owornmask & W_ARMOR))
                ia_ex_push(i, IA_WEAR_OBJ, 'W', "Wear");
            break;
        case IAEX_TAKEOFF:
            if (otmp->owornmask & W_ARMOR)
                ia_ex_push(i, IA_TAKEOFF_OBJ, 'T', "Take off");
            break;
        case IAEX_PUTON:
            if ((otmp->oclass == RING_CLASS
                 || otmp->oclass == AMULET_CLASS
                 || otmp->otyp == BLINDFOLD
                 || otmp->otyp == TOWEL
                 || otmp->otyp == LENSES)
                && !(otmp->owornmask & W_ACCESSORY))
                ia_ex_push(i, IA_WEAR_OBJ, 'P', "Put on");
            break;
        case IAEX_REMOVE:
            if (otmp->owornmask & W_ACCESSORY)
                ia_ex_push(i, IA_TAKEOFF_OBJ, 'R', "Remove");
            break;
        case IAEX_ZAP:
            if (otmp->oclass == WAND_CLASS)
                ia_ex_push(i, IA_ZAP_OBJ, 'z', "Zap");
            break;
        case IAEX_UNTRAP:
            if (otmp->otyp == CAN_OF_GREASE || otmp->otyp == POT_OIL)
                ia_ex_push(i, IA_UNTRAP_OBJ, 'u', "Untrap");
            break;
        case IAEX_OFFER:
            if (otmp->otyp == CORPSE
                || otmp->otyp == AMULET_OF_YENDOR
                || otmp->otyp == FAKE_AMULET_OF_YENDOR)
                ia_ex_push(i, IA_SACRIFICE, 'O', "Offer");
            break;
        case IAEX_NAME:
            ia_ex_push(i, IA_NAME_OBJ, 'c', "Name");
            break;
        case IAEX_CALL:
            if (ia_support_call(cat))
                ia_ex_push(i, IA_NAME_OTYP, 'C', "Call");
            break;
        case IAEX_ADJUST:
            ia_ex_push(i, IA_ADJUST_OBJ, 'i', "Adjust");
            break;
        case IAEX_ENGRAVE:
            if (ia_support_engrave(otmp))
                ia_ex_push(i, IA_ENGRAVE_OBJ, 'E', "Engrave");
            break;
        case IAEX_DIP:
            if (potion)
                ia_ex_push(i, IA_DIP_OBJ, 'a', "Dip");
            break;
        case IAEX_INFO:
            ia_ex_push(i, IA_WHATIS_OBJ, '/', "Info");
            break;
        }
    }
    ia_ex_emit(win, ia_primary_id(otmp));
}

/* Show a read-only information page for an item in its own window, so it
   closes with A/B instead of dumping into the message log. */
static void
ia_info_window(struct obj *otmp)
{
    winid win;
    anything any;
    char buf[BUFSZ];
    const char *classname;
    menu_item *dummy = (menu_item *) 0;

    win = create_nhwindow(NHW_MENU);
    start_menu(win, MENU_BEHAVE_STANDARD);

    any = cg.zeroany; /* text rows are not selectable */

    Snprintf(buf, sizeof buf, "%s", doname(otmp));
    add_menu(win, &nul_glyphinfo, &any, '\0', 0, ATR_NONE, NO_COLOR,
             buf, MENU_ITEMFLAGS_NONE);

    classname = let_to_name(def_oc_syms[(int) otmp->oclass].sym, FALSE, FALSE);
    Snprintf(buf, sizeof buf, "%s: %s", nh_tr("Class"), classname);
    add_menu(win, &nul_glyphinfo, &any, '\0', 0, ATR_NONE, NO_COLOR,
             buf, MENU_ITEMFLAGS_NONE);

    Snprintf(buf, sizeof buf, "%s: %ld", nh_tr("Count"), otmp->quan);
    add_menu(win, &nul_glyphinfo, &any, '\0', 0, ATR_NONE, NO_COLOR,
             buf, MENU_ITEMFLAGS_NONE);

    Snprintf(buf, sizeof buf, "%s: %d", nh_tr("Weight"), (int) weight(otmp));
    add_menu(win, &nul_glyphinfo, &any, '\0', 0, ATR_NONE, NO_COLOR,
             buf, MENU_ITEMFLAGS_NONE);

    if (otmp->bknown) {
        Snprintf(buf, sizeof buf, "%s: %s", nh_tr("Status"),
                 otmp->cursed ? nh_tr("cursed")
                 : otmp->blessed ? nh_tr("blessed") : nh_tr("uncursed"));
        add_menu(win, &nul_glyphinfo, &any, '\0', 0, ATR_NONE, NO_COLOR,
                 buf, MENU_ITEMFLAGS_NONE);
    }
    if (Is_container(otmp) && Has_contents(otmp)) {
        struct obj *o;
        long cc = 0L;

        for (o = otmp->cobj; o; o = o->nobj)
            cc++;
        Snprintf(buf, sizeof buf, nh_tr("Contains %ld items"), cc);
        add_menu(win, &nul_glyphinfo, &any, '\0', 0, ATR_NONE, NO_COLOR,
                 buf, MENU_ITEMFLAGS_NONE);
    }

    Snprintf(buf, sizeof buf, "%s", nh_tr("Press A or B to close."));
    add_menu(win, &nul_glyphinfo, &any, '\0', 0, ATR_NONE, NO_COLOR,
             buf, MENU_ITEMFLAGS_NONE);

    end_menu(win, nh_tr("Item information"));
    (void) select_menu(win, PICK_NONE, &dummy);
    if (dummy)
        free((genericptr_t) dummy);
    destroy_nhwindow(win);
}
#endif /* NINTENDO_3DS */

/* Show menu of possible actions hero could do with item otmp */
int
itemactions(struct obj *otmp)
{
    int n, act = IA_NONE;
    winid win;
    char buf[BUFSZ];
    menu_item *selected;

    win = create_nhwindow(NHW_MENU);
    ia_count = 0;
    start_menu(win, MENU_BEHAVE_STANDARD);

#if 0
    /* The 3DS port previously used the compact, category-filtered action
       list from the NetHack-3D example port (see ia_example_menu() above);
       it now uses the standard NetHack descriptive action menu below. */
    ia_example_menu(win, otmp);
#else
    {
    char buf2[BUFSZ];
    struct monst *mtmp;
    const char *light = otmp->lamplit ? "Extinguish" : "Light";
    boolean already_worn = (otmp->owornmask & (W_ARMOR | W_ACCESSORY)) != 0;

    /* -: unwield; picking current weapon offers an opportunity for 'w-'
       to wield bare/gloved hands; likewise for 'Q-' with quivered item(s) */
    if (otmp == uwep || otmp == uswapwep || otmp == uquiver) {
        const char *verb = (otmp == uquiver) ? "Quiver" : "Wield",
                   *action = (otmp == uquiver) ? "un-ready" : "un-wield",
                   *which = is_plural(otmp) ? "these" : "this",
                   *what = ((otmp->oclass == WEAPON_CLASS || is_weptool(otmp))
                            ? "weapon" : "item");
        /*
         * TODO: if uwep is ammo, tell player that to shoot instead of toss,
         *       the corresponding launcher must be wielded;
         */
        Sprintf(buf,  "%s '%c' to %s %s %s",
                verb, HANDS_SYM, action, which,
                is_plural(otmp) ? makeplural(what) : what);
        ia_addmenu(win, IA_UNWIELD, '-', buf);
    }

    /* a: apply */
    if (otmp->oclass == COIN_CLASS)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Flip a coin");
    else if (otmp->otyp == CREAM_PIE)
        ia_addmenu(win, IA_APPLY_OBJ, 'a',
                   "Hit yourself with this cream pie");
    else if (otmp->otyp == BULLWHIP)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Lash out with this whip");
    else if (otmp->otyp == GRAPPLING_HOOK)
        ia_addmenu(win, IA_APPLY_OBJ, 'a',
                   "Grapple something with this hook");
    else if (otmp->otyp == BAG_OF_TRICKS && objects[otmp->otyp].oc_name_known)
        /* bag of tricks skips this unless discovered */
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Reach into this bag");
    else if (Is_container(otmp))
        /* bag of tricks gets here only if not yet discovered */
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Open this container");
    else if (otmp->otyp == CAN_OF_GREASE)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Use the can to grease an item");
    else if (otmp->otyp == LOCK_PICK
             || otmp->otyp == CREDIT_CARD
             || otmp->otyp == SKELETON_KEY)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Use this tool to pick a lock");
    else if (otmp->otyp == TINNING_KIT)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Use this kit to tin a corpse");
    else if (otmp->otyp == LEASH) {
        if (!otmp->leashmon) {
            Strcpy(buf, "Attach this leash to a pet");
        } else {
            mtmp = find_mid(otmp->leashmon, FM_FMON);
            if (!mtmp) /* assume this won't happen */
                panic("Can't find leash's monster");
            Sprintf(buf, "Detach this leash from %s", some_mon_nam(mtmp));
        }
        ia_addmenu(win, IA_APPLY_OBJ, 'a', buf);
    } else if (otmp->otyp == SADDLE)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Place this saddle on a pet");
    else if (otmp->otyp == MAGIC_WHISTLE
             || otmp->otyp == TIN_WHISTLE)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Blow this whistle");
    else if (otmp->otyp == EUCALYPTUS_LEAF)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Use this leaf as a whistle");
    else if (otmp->otyp == STETHOSCOPE)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Listen through the stethoscope");
    else if (otmp->otyp == MIRROR)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Show something its reflection");
    else if (otmp->otyp == BELL || otmp->otyp == BELL_OF_OPENING)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Ring the bell");
    else if (otmp->otyp == CANDELABRUM_OF_INVOCATION) {
        Sprintf(buf, "%s the candelabrum", light);
        ia_addmenu(win, IA_APPLY_OBJ, 'a', buf);
    } else if (otmp->otyp == WAX_CANDLE || otmp->otyp == TALLOW_CANDLE) {
        boolean multiple = (otmp->quan == 1L) ? FALSE : TRUE;
        const char *s = multiple ? "these" : "this";
        struct obj *o = carrying(CANDELABRUM_OF_INVOCATION);

        if (o && o->spe < 7)
            Sprintf(buf, "Attach %s to your candelabrum, or %s %s", s,
                    !otmp->lamplit ? "light" : "extinguish", /* [lowercase] */
                    multiple ? "them" : "it");
        else
            Sprintf(buf, "%s %s %s", light, s, simpleonames(otmp));
        ia_addmenu(win, IA_APPLY_OBJ, 'a', buf);
    } else if (otmp->otyp == OIL_LAMP || otmp->otyp == MAGIC_LAMP
               || otmp->otyp == BRASS_LANTERN) {
        Sprintf(buf, "%s this light source", light);
        ia_addmenu(win, IA_APPLY_OBJ, 'a', buf);
    } else if (otmp->otyp == POT_OIL && objects[otmp->otyp].oc_name_known) {
        Sprintf(buf, "%s this oil", light);
        ia_addmenu(win, IA_APPLY_OBJ, 'a', buf);
    } else if (otmp->oclass == POTION_CLASS) {
        /* FIXME? this should probably be moved to 'D' rather than be 'a' */
        Sprintf(buf, "Dip something into %s potion%s",
                is_plural(otmp) ? "one of these" : "this", plur(otmp->quan));
        ia_addmenu(win, IA_DIP_OBJ, 'a', buf);
    } else if (otmp->otyp == EXPENSIVE_CAMERA)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Take a photograph");
    else if (otmp->otyp == TOWEL)
        ia_addmenu(win, IA_APPLY_OBJ, 'a',
                   "Clean yourself off with this towel");
    else if (otmp->otyp == CRYSTAL_BALL)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Peer into this crystal ball");
    else if (otmp->otyp == MAGIC_MARKER)
        ia_addmenu(win, IA_APPLY_OBJ, 'a',
                   "Write on something with this marker");
    else if (otmp->otyp == FIGURINE)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Make this figurine transform");
    else if (otmp->otyp == UNICORN_HORN)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Use this unicorn horn");
    else if (otmp->otyp == HORN_OF_PLENTY
             && objects[otmp->otyp].oc_name_known)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Blow into the horn of plenty");
    else if (otmp->otyp >= WOODEN_FLUTE && otmp->otyp <= DRUM_OF_EARTHQUAKE)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Play this musical instrument");
    else if (otmp->otyp == LAND_MINE || otmp->otyp == BEARTRAP)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Arm this trap");
    else if (otmp->otyp == PICK_AXE || otmp->otyp == DWARVISH_MATTOCK)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Dig with this digging tool");
    else if (otmp->oclass == WAND_CLASS)
        ia_addmenu(win, IA_APPLY_OBJ, 'a', "Break this wand");

    /* 'c', 'C' - call an item or its type something */
    if (item_naming_classification(otmp, buf, buf2)) {
        if (*buf)
            ia_addmenu(win, IA_NAME_OBJ, 'c', buf);
        if (*buf2)
            ia_addmenu(win, IA_NAME_OTYP, 'C', buf2);
    }

    /* d: drop item, works on everything except worn items; those will
       always have a takeoff/remove choice so we don't have to worry
       about the menu maybe being empty when 'd' is suppressed */
    if (!already_worn) {
        Sprintf(buf, "Drop this %s", (otmp->quan > 1L) ? "stack" : "item");
        ia_addmenu(win, IA_DROP_OBJ, 'd', buf);
    }

    /* e: eat item */
    if (otmp->otyp == TIN) {
        Sprintf(buf, "Open %s%s and eat the contents",
                (otmp->quan > 1L) ? "one of these tins" : "this tin",
                (otmp->otyp == TIN && uwep && uwep->otyp == TIN_OPENER)
                ? " with your tin opener" : "");
        ia_addmenu(win, IA_EAT_OBJ, 'e', buf);
    } else if (is_edible(otmp)) {
        Sprintf(buf, "Eat %s", (otmp->quan > 1L) ? "one of these" : "this");
        ia_addmenu(win, IA_EAT_OBJ, 'e', buf);
    }

    /* E: engrave with item */
    if (otmp->otyp == TOWEL) {
        ia_addmenu(win, IA_ENGRAVE_OBJ, 'E',
                   "Wipe the floor with this towel");
    } else if (otmp->otyp == MAGIC_MARKER) {
        ia_addmenu(win, IA_ENGRAVE_OBJ, 'E',
                   "Scribble graffiti on the floor");
    } else if (otmp->oclass == WEAPON_CLASS || otmp->oclass == WAND_CLASS
             || otmp->oclass == GEM_CLASS || otmp->oclass == RING_CLASS) {
        Sprintf(buf, "%s on the %s with %s",
                (is_blade(otmp) || otmp->oclass == WAND_CLASS
                 || ((otmp->oclass == GEM_CLASS || otmp->oclass == RING_CLASS)
                     && objects[otmp->otyp].oc_tough)) ? "Engrave" : "Write",
                surface(u.ux, u.uy),
                (otmp->quan > 1L) ? "one of these items" : "this item");
        ia_addmenu(win, IA_ENGRAVE_OBJ, 'E', buf);
    }

    /* f: fire quivered ammo */
    if (otmp == uquiver) {
        boolean shoot = ammo_and_launcher(otmp, uwep);

        /* FIXME: see the multi-shot FIXME about "one of" for 't: throw' */
        Sprintf(buf, "%s %s", shoot ? "Shoot" : "Throw",
                (otmp->quan > 1L) ? "one of these" : "this");
        if (shoot) {
            assert(uwep != NULL);
            Sprintf(eos(buf), " with your wielded %s", simpleonames(uwep));
        }
        ia_addmenu(win, IA_FIRE_OBJ, 'f', buf);
    }

    /* i: #adjust inventory letter; gold can't be adjusted unless there
       is some in a slot other than '$' (which shouldn't be possible) */
    if (otmp->oclass != COIN_CLASS || check_invent_gold("item-action"))
        ia_addmenu(win, IA_ADJUST_OBJ, 'i',
                   "Adjust inventory by assigning new letter");
    /* I: #adjust inventory item by splitting its stack  */
    if (otmp->quan > 1L && otmp->oclass != COIN_CLASS)
        ia_addmenu(win, IA_ADJUST_STACK, 'I',
                   "Adjust inventory by splitting this stack");

    /* O: offer sacrifice */
    if (IS_ALTAR(levl[u.ux][u.uy].typ) && !u.uswallow) {
        /* FIXME: this doesn't match #offer's likely candidates, which don't
           include corpses on Astral and don't include amulets off Astral */
        if (otmp->otyp == CORPSE)
            ia_addmenu(win, IA_SACRIFICE, 'O',
                       "Offer this corpse as a sacrifice at this altar");
        else if (otmp->otyp == AMULET_OF_YENDOR
                 || otmp->otyp == FAKE_AMULET_OF_YENDOR)
            ia_addmenu(win, IA_SACRIFICE, 'O',
                       "Offer this amulet as a sacrifice at this altar");
    }

    /* p: pay for unpaid utems */
    if (otmp->unpaid
        /* FIXME: should also handle player owned container (so not
           flagged 'unpaid') holding shop owned items */
        && (mtmp = shop_keeper(*in_rooms(u.ux, u.uy, SHOPBASE))) != 0
        && inhishop(mtmp)) {
        Sprintf(buf, "Buy this unpaid %s",
                (otmp->quan > 1L) ? "stack" : "item");
        ia_addmenu(win, IA_BUY_OBJ, 'p', buf);
    }

    /* P: put on accessory */
    if (!already_worn) {
        /* if 'otmp' is worn, we'll skip 'P' and show 'R' below;
           if not worn, we show 'P - Put on this <simple-item>' if
           the slot is available, or 'P - <unavailable>'; for the latter,
           'P' will fail but we don't want to omit the choice because
           item actions can be used to learn commands */
        *buf = '\0';
        if (otmp->oclass == AMULET_CLASS) {
            Strcpy(buf, !uamul ? "Put this amulet on"
                               : "[already wearing an amulet]");
        } else if (otmp->oclass == RING_CLASS || otmp->otyp == MEAT_RING) {
            if (!uleft || !uright)
                Strcpy(buf, "Put this ring on");
            else
                Sprintf(buf, "[both ring %s in use]",
                        makeplural(body_part(FINGER)));
        } else if (otmp->otyp == BLINDFOLD || otmp->otyp == TOWEL
                   || otmp->otyp == LENSES) {
            if (ublindf)
                Strcpy(buf, "[already wearing eyewear]");
            else if (otmp->otyp == LENSES)
                Strcpy(buf, "Put these lenses on");
            else
                Sprintf(buf, "Put this on%s",
                        (otmp->otyp == TOWEL) ? " to blindfold yourself" : "");
        }
        if (*buf)
            ia_addmenu(win, IA_WEAR_OBJ, 'P', buf);
    }

    /* q: drink item */
    if (otmp->oclass == POTION_CLASS) {
        Sprintf(buf, "Quaff (drink) %s",
                (otmp->quan > 1L) ? "one of these potions" : "this potion");
        ia_addmenu(win, IA_QUAFF_OBJ, 'q', buf);
    }

    /* Q: quiver throwable item */
    if ((otmp->oclass == GEM_CLASS || otmp->oclass == WEAPON_CLASS)
        && otmp != uquiver) {
        Sprintf(buf, "Quiver this %s for easy %s with \'f\'ire",
                (otmp->quan > 1L) ? "stack" : "item",
                ammo_and_launcher(otmp, uwep) ? "shooting" : "throwing");
        ia_addmenu(win, IA_QUIVER_OBJ, 'Q', buf);
    }

    /* r: read item */
    if (item_reading_classification(otmp, buf) == IA_READ_OBJ)
        ia_addmenu(win, IA_READ_OBJ, 'r', buf);

    /* R: remove accessory or rub item */
    if (otmp->owornmask & W_ACCESSORY) {
        Sprintf(buf, "Remove this %s",
                (otmp->owornmask & W_AMUL) ? "amulet"
                : (otmp->owornmask & W_RING) ? "ring"
                  : (otmp->owornmask & W_TOOL) ? "eyewear"
                    : "accessory"); /* catchall -- can't happen */
        ia_addmenu(win, IA_TAKEOFF_OBJ, 'R', buf);
    }
    if (otmp->otyp == OIL_LAMP || otmp->otyp == MAGIC_LAMP
        || otmp->otyp == BRASS_LANTERN) {
        Sprintf(buf, "Rub this %s", simpleonames(otmp));
        ia_addmenu(win, IA_RUB_OBJ, 'R', buf);
    } else if (otmp->oclass == GEM_CLASS && is_graystone(otmp))
        ia_addmenu(win, IA_RUB_OBJ, 'R', "Rub something on this stone");

    /* t: throw item */
    if (!already_worn) {
        boolean shoot = ammo_and_launcher(otmp, uwep);

        /*
         * FIXME:
         *  'one of these' should be changed to 'some of these' when there
         *  is the possibility of a multi-shot volley but we don't have
         *  any way to determine that except by actually calculating the
         *  volley count and that could randomly yield 1 here and 2..N
         *  while throwing or vice versa.
         */
        Sprintf(buf, "%s %s%s", shoot ? "Shoot" : "Throw",
                (otmp->quan == 1L) ? "this item"
                : (otmp->otyp == GOLD_PIECE) ? "them"
                  : "one of these",
                /* if otmp is quivered, we've already listed
                   'f - shoot|throw this item' as a choice;
                   if 't' is duplicating that, say so ('t' and 'f'
                   behavior differs for throwing a stack of gold) */
                (otmp == uquiver && (otmp->otyp != GOLD_PIECE
                                     || otmp->quan == 1L))
                ? " (same as 'f')" : "");
        ia_addmenu(win, IA_THROW_OBJ, 't', buf);
    }

    /* T: take off armor, tip carried container */
    if (otmp->owornmask & W_ARMOR)
        ia_addmenu(win, IA_TAKEOFF_OBJ, 'T', "Take off this armor");
    if ((Is_container(otmp) && (Has_contents(otmp) || !otmp->cknown))
        || (otmp->otyp == HORN_OF_PLENTY && (otmp->spe > 0 || !otmp->known)))
        ia_addmenu(win, IA_TIP_CONTAINER, 'T',
                   "Tip all the contents out of this container");

    /* V: invoke */
    if ((otmp->otyp == FAKE_AMULET_OF_YENDOR && !otmp->known)
        || otmp->oartifact || objects[otmp->otyp].oc_unique
        /* non-artifact crystal balls don't have any unique power but
           the #invoke command lists them as likely candidates */
        || otmp->otyp == CRYSTAL_BALL)
        ia_addmenu(win, IA_INVOKE_OBJ, 'V',
                   "Try to invoke a unique power of this object");

    /* w: wield, hold in hands, works on everything but with different
       advice text; not mentioned for things that are already wielded */
    if (otmp == uwep || cantwield(gy.youmonst.data)) {
        ; /* either already wielded or can't wield anything; skip 'w' */
    } else if (otmp->oclass == WEAPON_CLASS || is_weptool(otmp)
               || is_wet_towel(otmp) || otmp->otyp == HEAVY_IRON_BALL) {
        Sprintf(buf, "Wield this %s as your weapon",
                (otmp->quan > 1L) ? "stack" : "item");
        ia_addmenu(win, IA_WIELD_OBJ, 'w', buf);
    } else if (otmp->otyp == TIN_OPENER) {
        ia_addmenu(win, IA_WIELD_OBJ, 'w',
                   "Wield the tin opener to easily open tins");
    } else if (!already_worn) {
        /* originally this was using "hold this item in your hands" but
           there's no concept of "holding an item", plus it unwields
           whatever item you already have wielded so use "wield this item" */
        Sprintf(buf, "Wield this %s in your %s",
                (otmp->quan > 1L) ? "stack" : "item",
                /* only two-handed weapons and unicorn horns care about
                   pluralizing "hand" and they won't reach here, but plural
                   sounds better when poly'd into something with "claw" */
                makeplural(body_part(HAND)));
        ia_addmenu(win, IA_WIELD_OBJ, 'w', buf);
    }

    /* W: wear armor */
    if (!already_worn) {
        if (otmp->oclass == ARMOR_CLASS) {
            /* if 'otmp' is worn we skip 'W' (and show 'T' above instead);
               if it isn't, we either show "W - wear this" if otmp's slot
               isn't populated, or "W - [already wearing <simple-armor>]";
               for the latter, picking 'W' will fail but we don't want to
               omit 'W' in this situation */
            long Wmask = armcat_to_wornmask(objects[otmp->otyp].oc_armcat);
            struct obj *o = wearmask_to_obj(Wmask);

            if (!o)
                Strcpy(buf, "Wear this armor");
            else
                Sprintf(buf, "[already wearing %s]", an(armor_simple_name(o)));

            ia_addmenu(win, IA_WEAR_OBJ, 'W', buf);
        }
    }

    /* x: Swap main and readied weapon */
    if (otmp == uwep && uswapwep)
        ia_addmenu(win, IA_SWAPWEAPON, 'x',
                   "Swap this with your alternate weapon");
    else if (otmp == uwep)
        ia_addmenu(win, IA_SWAPWEAPON, 'x',
                   "Ready this as an alternate weapon");
    else if (otmp == uswapwep)
        ia_addmenu(win, IA_SWAPWEAPON, 'x',
                   "Swap this with your main weapon");

    /* this is based on TWOWEAPOK() in wield.c; we don't call can_two_weapon()
       because it is very verbose; attempting to two-weapon might be rejected
       but we screen out most reasons for rejection before offering it as a
       choice */
#define MAYBETWOWEAPON(obj) \
    ((((obj)->oclass == WEAPON_CLASS)                           \
      ? !(is_launcher(obj) || is_ammo(obj) || is_missile(obj))  \
      : is_weptool(obj))                                        \
     && !bimanual(obj))

    /* X: Toggle two-weapon mode on or off */
    if ((otmp == uwep || otmp == uswapwep)
        /* if already two-weaponing, no special checks needed to toggle off */
        && (u.twoweap
        /* but if not, try to filter most "you can't do that" here */
            || (could_twoweap(gy.youmonst.data) && !uarms
                && uwep && MAYBETWOWEAPON(uwep)
                && uswapwep && MAYBETWOWEAPON(uswapwep)))) {
        Sprintf(buf, "Toggle two-weapon combat %s", u.twoweap ? "off" : "on");
        ia_addmenu(win, IA_TWOWEAPON, 'X', buf);
    }

#undef MAYBETWOWEAPON

    /* z: Zap wand */
    if (otmp->oclass == WAND_CLASS)
        ia_addmenu(win, IA_ZAP_OBJ, 'z',
                   "Zap this wand to release its magic");

    /* ?: Look up an item in the game's database */
    if (ia_checkfile(otmp)) {
        Sprintf(buf, "Look up information about %s",
                (otmp->quan > 1L) ? "these" : "this");
        ia_addmenu(win, IA_WHATIS_OBJ, '/', buf);
    }

    Sprintf(buf, "Do what with %s?", the(cxname(otmp)));
    ia_flush_menu(win, otmp);
    }
#endif /* !(3DS compact action menu) */
#ifdef NINTENDO_3DS
    _3ds_menu_title_override("Context menu");
#endif
    end_menu(win, buf);

    n = select_menu(win, PICK_ONE, &selected);
#ifdef NINTENDO_3DS
    _3ds_menu_title_override(NULL);
#endif

    if (n > 0) {
        act = selected[0].item.a_int;
        free((genericptr_t) selected);

#ifdef NINTENDO_3DS
        if (act == IA_WHATIS_OBJ) {
            /* show the item information in its own window instead of
               dumping it into the message log */
            ia_info_window(otmp);
        } else
#endif
        itemactions_pushkeys(otmp, act);
    }
    destroy_nhwindow(win);

    /* finish the 'i' command:  no time elapses and cancelling without
       selecting an action doesn't matter */
    return ECMD_OK;
}

/*iactions.c*/
