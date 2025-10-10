#include "../world.h"

typedef struct {
  Attributes attr;
} CombatStats;

static Attributes gather_attributes(EntityIndex entity,
                                    EntitySet *entity_tree) {
  Attributes attr = {0};

  if (entity_has(entity, attributes)) {
    attr = WORLD.attributes[entity];

    entityset_query(i, entity_tree, HAS(attributes_modifier)) {
      if (get_attributes_ancestor(i) == entity) {
        attr.str =
            clamp_int(attr.str + WORLD.attributes_modifier[i].str, 0, STR_MAX);
        attr.dex =
            clamp_int(attr.dex + WORLD.attributes_modifier[i].dex, 0, DEX_MAX);
        attr.wil =
            clamp_int(attr.wil + WORLD.attributes_modifier[i].wil, 0, WIL_MAX);
        attr.con =
            clamp_int(attr.con + WORLD.attributes_modifier[i].con, 0, CON_MAX);
      }
    }
  } else {
    attr.str = STR_DEFAULT;
    attr.dex = DEX_DEFAULT;
    attr.wil = WIL_DEFAULT;
    attr.con = CON_DEFAULT;
  }

  return attr;
}

static CombatStats gather_combat_stats(EntityIndex combatant) {
  CombatStats stats = {0};

  EntitySet combatant_tree = {0};
  entityset_add(&combatant_tree, combatant);
  entityset_expand_descendants(&combatant_tree);

  stats.attr = gather_attributes(combatant, &combatant_tree);

  // TODO: find and apply more bonuses or effects

  return stats;
}

void action_combat(EntityIndex attacker, EntityIndex defender) {
  turn_queue_add_delay(attacker, TURN_INTERVAL);

  WORLD.anim =
      (ActionAnim){.type = ACTION_ANIM_ATTACK,
                   .actor = entity_handle_from_index(attacker),
                   .attack = {.target = entity_handle_from_index(defender)}};

  if (attacker == entity_handle_to_index(WORLD.player)) {
    output_message("You attacked!");
  }

  CombatStats attacker_stats = gather_combat_stats(attacker);
  CombatStats defender_stats = gather_combat_stats(defender);

  int damage = attacker_stats.attr.str - defender_stats.attr.dex / 2;

  if (entity_has(defender, health)) {
    // operate on full int to allow for negative numbers
    int health = WORLD.health[defender];
    health -= damage;
    if (health < 0) {
      health = 0;
    }
    WORLD.health[defender] = health;

    if (health == 0) {
      entity_mark(defender, is_dead);
      if (entity_is_player(defender)) {
        output_message("You died!");
      }
    }
  }
}
