#ifndef LINKSPAN_OOT_ENGINE_H
#define LINKSPAN_OOT_ENGINE_H
#include <shiplua/native/ship_native_abi.h>

#define LINKSPAN_OOT_ENGINE_SERVICE "linkspan.oot.engine"
#define LINKSPAN_OOT_ENGINE_VERSION 1u
#define LINKSPAN_OOT_MOVEMENT_SERVICE "linkspan.oot.movement"
#define LINKSPAN_OOT_MOVEMENT_VERSION 1u
#define LINKSPAN_OOT_MOVEMENT_VERSION_2 2u

/* Botões aceitos por get_item_button_rect: índices de SaveContext.equips.buttonItems. */
#define LINKSPAN_OOT_ITEM_BUTTON_C_LEFT 1u
#define LINKSPAN_OOT_ITEM_BUTTON_C_DOWN 2u
#define LINKSPAN_OOT_ITEM_BUTTON_C_RIGHT 3u

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

/* Prefixo binário compatível com ShipOotMovementV1 (OOT-MOVE-004).
 *
 * get_gamepad_axis: valor SDL bruto (-32768..32767) do eixo físico com maior
 *   magnitude entre os gamepads da porta; 0 sem gamepad ou com input bloqueado.
 * bind_gamepad_axis: liga só em memória metade de um eixo SDL a um botão virtual
 *   (direction 1 = positiva, -1 = negativa) com o limiar global do SDL;
 *   clear_gamepad_button_bindings e reload_gamepad_mappings também o desfazem.
 * player_use_item_shortcut: usa ITEM_LENS ou ITEM_MASK_* como se o item estivesse
 *   num botão C habilitado, sem exigir que esteja. Aplica as condições do jogo
 *   (Player, cena, idade, inventário e magia) e retorna SHIP_NATIVE_UNSUPPORTED
 *   quando o jogo recusaria o uso ou nada mudou. A lente ligada assim segue ativa
 *   fora dos botões; máscara fora dos botões exige gEnhancements.PersistentMasks,
 *   sem a qual o jogo a tiraria no frame seguinte.
 * get_item_button_rect: posição no HUD, lado e alpha do botão C desenhado no frame
 *   atual ou no anterior; botão oculto ou HUD não desenhado retorna
 *   SHIP_NATIVE_UNSUPPORTED. */
typedef struct ShipOotMovementV2 {
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
    int16_t (SHIP_NATIVE_CALL *get_gamepad_axis)(uint8_t port, uint8_t sdl_axis);
    ShipNativeStatus (SHIP_NATIVE_CALL *bind_gamepad_axis)(uint8_t port, uint16_t virtual_button,
                                                          uint8_t sdl_axis, int8_t direction);
    ShipNativeStatus (SHIP_NATIVE_CALL *player_use_item_shortcut)(uint8_t item);
    ShipNativeStatus (SHIP_NATIVE_CALL *get_item_button_rect)(uint8_t button, int16_t* x, int16_t* y,
                                                             int16_t* size, uint8_t* alpha);
} ShipOotMovementV2;
#endif
