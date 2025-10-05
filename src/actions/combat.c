#include "../world.h"

typedef struct {
  Attributes attr;
} CombatStats;

static CombatStats gather_combat_stats(EntityIndex combatant) {
  CombatStats stats = {0};
  EntitySet subtree = {0};

  if (entity_has(combatant, attributes)) {
    stats.attr = world.attributes[combatant];
  } else {
    stats.attr.str = STR_DEFAULT;
    stats.attr.dex = DEX_DEFAULT;
    stats.attr.wil = WIL_DEFAULT;
    stats.attr.con = CON_DEFAULT;
  }

  entityset_add(&subtree, combatant);
  entityset_expand_descendants(&subtree);

  entityset_query(i, &subtree, HAS(attributes_modifier)) {
    stats.attr.str += world.attributes_modifier[i].str;
    stats.attr.dex += world.attributes_modifier[i].dex;
    stats.attr.wil += world.attributes_modifier[i].wil;
    stats.attr.con += world.attributes_modifier[i].con;
  }

  // TODO: find and apply more bonuses or effects

  return stats;
}

void action_combat(EntityIndex attacker, EntityIndex defender) {
  CombatStats attacker_stats = gather_combat_stats(attacker);
  CombatStats defender_stats = gather_combat_stats(defender);

  int damage = attacker_stats.attr.str - defender_stats.attr.dex / 2;

  if (entity_has(defender, health)) {
    // operate on full int to allow for negative numbers
    int health = world.health[defender];
    health -= damage;
    if (health < 0) {
      health = 0;
    }
    world.health[defender] = health;

    if (health == 0) {
      entity_mark(defender, is_dead);
      if (entity_is_player(defender)) {
        output_message("You died!");
      }
    }
  }
}
