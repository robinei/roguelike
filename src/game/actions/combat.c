#include "../world.h"

typedef struct {
  Attributes attr;
} CombatStats;

static Attributes gather_attributes(EntityIndex entity,
                                    EntitySet *entity_tree) {
  Attributes attr = {0};

  if (HAS_PART(Attributes, entity)) {
    attr = PART(Attributes, entity);

    entityset_query(i, entity_tree, HAS(AttributesModifier)) {
      if (get_attributes_ancestor(i) == entity) {
        attr.str =
            clamp_int(attr.str + PART(AttributesModifier, i).str, 0, STR_MAX);
        attr.dex =
            clamp_int(attr.dex + PART(AttributesModifier, i).dex, 0, DEX_MAX);
        attr.wil =
            clamp_int(attr.wil + PART(AttributesModifier, i).wil, 0, WIL_MAX);
        attr.con =
            clamp_int(attr.con + PART(AttributesModifier, i).con, 0, CON_MAX);
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

  if (attacker == entity_handle_to_index(WORLD.entities.player)) {
    output_message("You attacked!");
  }

  CombatStats attacker_stats = gather_combat_stats(attacker);
  CombatStats defender_stats = gather_combat_stats(defender);

  int damage = attacker_stats.attr.str - defender_stats.attr.dex / 2;

  if (HAS_PART(Health, defender)) {
    // operate on full int to allow for negative numbers
    int health = PART(Health, defender);
    health -= damage;
    if (health < 0) {
      health = 0;
    }
    PART(Health, defender) = health;

    if (health == 0) {
      SET_MARK(IsDead, defender);
      if (entity_is_player(defender)) {
        output_message("You died!");
      }
    }
  }
}
