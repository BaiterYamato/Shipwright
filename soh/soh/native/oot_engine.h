#ifndef LINKSPAN_OOT_ENGINE_H
#define LINKSPAN_OOT_ENGINE_H
#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_ENGINE_SERVICE "linkspan.oot.engine"
#define LINKSPAN_OOT_ENGINE_VERSION 1u
#define LINKSPAN_OOT_MOVEMENT_SERVICE "linkspan.oot.movement"
#define LINKSPAN_OOT_MOVEMENT_VERSION 1u

/* Apenas código nativo na thread do jogo. Ponteiros não sobrevivem a cenas/frames.
 * Consulte layout_id e sizeof antes de converter para os tipos dos headers do host.
 * O provider é responsável por suas escritas; este serviço não é uma sandbox. */
typedef struct ShipOotEngineV1 {
    uint32_t size;
    const char* layout_id;
    uint32_t play_state_size;
    uint32_t player_size;
    uint32_t save_context_size;
    void* (SHIP_NATIVE_CALL *get_play_state)(void);
    void* (SHIP_NATIVE_CALL *get_player)(void);
    void* (SHIP_NATIVE_CALL *get_save_context)(void);
    void* (SHIP_NATIVE_CALL *spawn_actor)(int16_t id, float x, float y, float z,
                                         int16_t rx, int16_t ry, int16_t rz, int16_t params);
    ShipNativeStatus (SHIP_NATIVE_CALL *kill_actor)(void* actor);
} ShipOotEngineV1;

/* Serviço separado para primitivas reutilizáveis de input e movimento. Manter
 * outro nome preserva linkspan.oot.engine v1 e a regra de nomes únicos do host. */
typedef struct ShipOotMovementV1 {
    uint32_t size;
    const char* layout_id;
    uint16_t (SHIP_NATIVE_CALL *get_input_current)(uint8_t port);
    uint16_t (SHIP_NATIVE_CALL *get_input_pressed)(uint8_t port);
    uint16_t (SHIP_NATIVE_CALL *get_input_released)(uint8_t port);
    int8_t (SHIP_NATIVE_CALL *get_stick_x)(uint8_t port);
    int8_t (SHIP_NATIVE_CALL *get_stick_y)(uint8_t port);
    int8_t (SHIP_NATIVE_CALL *get_right_stick_x)(uint8_t port);
    int8_t (SHIP_NATIVE_CALL *get_right_stick_y)(uint8_t port);
    uint8_t (SHIP_NATIVE_CALL *has_gamepad)(uint8_t port);
    uint32_t (SHIP_NATIVE_CALL *get_gamepad_buttons)(uint8_t port);
    ShipNativeStatus (SHIP_NATIVE_CALL *clear_gamepad_button_bindings)(uint8_t port, uint16_t virtual_button);
    ShipNativeStatus (SHIP_NATIVE_CALL *bind_gamepad_button)(uint8_t port, uint16_t virtual_button,
                                                            uint8_t sdl_button);
    ShipNativeStatus (SHIP_NATIVE_CALL *reload_gamepad_mappings)(uint8_t port);
    int32_t (SHIP_NATIVE_CALL *get_setting_int)(const char* name, int32_t fallback);
    ShipNativeStatus (SHIP_NATIVE_CALL *set_setting_int)(const char* name, int32_t value);
    uint8_t (SHIP_NATIVE_CALL *is_player_grounded)(void);
    uint8_t (SHIP_NATIVE_CALL *is_player_rolling)(void);
    ShipNativeStatus (SHIP_NATIVE_CALL *player_jump)(float vertical_velocity);
    ShipNativeStatus (SHIP_NATIVE_CALL *player_roll)(void);
} ShipOotMovementV1;
#endif
