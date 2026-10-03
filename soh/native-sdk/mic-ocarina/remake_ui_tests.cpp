// Exercise the real detours: replacing graphics must never skip song state updates.
#include "provider.cpp"
#include <memory>
#include <cstdlib>

std::string MicAssetsDirectory() { return {}; }
namespace {
PlayState* testPlay;
SaveContext* testSave;
std::uint32_t physical = 0;
bool gamepad = true, mapping = true;
int drawCalls = 0, readCalls = 0, submits = 0, shortcuts = 0;
uint32_t nativeReadButtons = MicRemake::NoteMask | BTN_R | BTN_Z | BTN_B;
bool active = true, dialogueAfterDraw = false;
bool checkMessageCancel = false, messageCancelled = false;
void* SHIP_NATIVE_CALL Play() { return testPlay; }
void* SHIP_NATIVE_CALL Save() { return testSave; }
uint8_t SHIP_NATIVE_CALL HasPad(uint8_t) { return gamepad; }
uint32_t SHIP_NATIVE_CALL Buttons(uint8_t) { return physical; }
uint16_t SHIP_NATIVE_CALL Virtual(uint8_t) { return 0; }
int16_t SHIP_NATIVE_CALL Axis(uint8_t, uint8_t) { return 0; }
uint8_t SHIP_NATIVE_CALL Active() { return active; }
uint16_t SHIP_NATIVE_CALL Flags() { return 0xFFF; }
ShipNativeStatus SHIP_NATIVE_CALL Submit(uint8_t) { ++submits; return SHIP_NATIVE_OK; }
ShipNativeStatus SHIP_NATIVE_CALL Shortcut(uint8_t item) {
    ++shortcuts; return item == ITEM_OCARINA_TIME ? SHIP_NATIVE_OK : SHIP_NATIVE_UNSUPPORTED;
}
ShipNativeStatus SHIP_NATIVE_CALL Writer(void* writer, const char* text, uint32_t length) {
    static_cast<std::string*>(writer)->append(text, length); return SHIP_NATIVE_OK;
}
void Draw(PlayState* play, Gfx** list) {
    ++drawCalls; ++play->msgCtx.stateTimer; ++*list;
    if (checkMessageCancel && (play->state.input[0].press.button & BTN_B)) {
        messageCancelled = true;
        play->msgCtx.ocarinaMode = OCARINA_MODE_04;
    }
    if (dialogueAfterDraw) play->msgCtx.msgMode = MSGMODE_TEXT_DONE;
}
void Read() { ++readCalls; *activeMod->nativeInputButtons = nativeReadButtons; }
void Mapping(bool custom) { mapping = custom; }
void Check(bool condition, const char* label) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(EXIT_FAILURE); }
}
}
int main() {
    for (const auto pair : { std::pair{9, 0}, {10, 1}, {1, 2}, {2, 3}, {3, 4} })
        Check(MicRemake::HeldNote(MicRemake::MapNotes(MicRemake::Physical(pair.first))) == pair.second,
              "L/R/A/Y/X map to the native D/F/A/B/D pitches");
    Check(MicRemake::MapNotes(MicRemake::Physical(0) | MicRemake::Physical(6)) == 0, "Cancel/Start are never notes");
    Check(MicRemake::LearnedSongs(1u << QUEST_SONG_LULLABY, false) == (1u << 8), "Lullaby quest maps to song 8");
    Check(MicRemake::LearnedSongs(1u << QUEST_SONG_SARIA, false) == (1u << 6), "Saria quest maps to song 6");
    Check(MicRemake::LearnedSongs(0, false) == 0 && MicRemake::LearnedSongs(0, true) == (1u << 12),
          "Songbook hides unlearned songs and checks Scarecrow availability");
    Check(MicRemake::CanBrowse(MSGMODE_OCARINA_PLAYING, OCARINA_ACTION_FREE_PLAY) &&
          !MicRemake::CanBrowse(MSGMODE_FROGS_PLAYING, OCARINA_ACTION_FREE_PLAY), "Songbook only in free play");

    const auto play = std::make_unique<PlayState>();
    const auto save = std::make_unique<SaveContext>();
    testPlay = play.get(); testSave = save.get(); save->gameMode = GAMEMODE_NORMAL;
    play->msgCtx.msgMode = MSGMODE_OCARINA_PLAYING;
    play->msgCtx.ocarinaAction = OCARINA_ACTION_FREE_PLAY;
    ShipOotEngineV1 engine{}; engine.get_play_state = Play; engine.get_save_context = Save;
    ShipOotMovementV2 movement{};
    movement.has_gamepad = HasPad; movement.get_gamepad_buttons = Buttons;
    movement.get_input_current = Virtual; movement.get_gamepad_axis = Axis;
    movement.player_use_item_shortcut = Shortcut;
    ShipOotOcarinaV1 ocarina{}; ocarina.is_active = Active; ocarina.get_available_song_flags = Flags; ocarina.submit_song = Submit;
    const auto owned = std::make_unique<Mod>(); auto& mod = *owned;
    mod.engine = &engine; mod.movement = &movement; mod.ocarina = &ocarina;
    mod.originalMessageDraw = Draw; mod.originalReadInput = Read; mod.originalButtonMapping = Mapping;
    uint32_t input = 0; mod.nativeInputButtons = &input; activeMod = &mod;
    Gfx commands[4]{}; Gfx* cursor = commands;
    RemakeMessageDraw(play.get(), &cursor);
    Check(drawCalls == 1 && play->msgCtx.stateTimer == 1 && cursor == commands,
          "Replacement preserves native song state while discarding stock music graphics");
    dialogueAfterDraw = true;
    RemakeMessageDraw(play.get(), &cursor);
    Check(cursor == commands + 1 && drawCalls == 2, "Transition to a dialogue prompt preserves its graphics");
    cursor = commands; dialogueAfterDraw = false;
    RemakeMessageDraw(play.get(), &cursor);
    Check(cursor == commands + 1, "Ordinary dialogue is never discarded");
    play->msgCtx.msgMode = MSGMODE_OCARINA_PLAYING; mod.remakeHud = false; cursor = commands;
    RemakeMessageDraw(play.get(), &cursor);
    Check(cursor == commands + 1, "Disabling remake HUD restores the stock renderer");
    mod.remakeHud = true;
    physical = MicRemake::Physical(2); RemakeReadInput();
    Check(readCalls == 1 && input == BTN_CLEFT, "Y plays C-left without inherited gameplay modifiers or cancel");
    checkMessageCancel = true;
    play->state.input[0].cur.button = play->state.input[0].press.button = BTN_B;
    play->msgCtx.ocarinaMode = OCARINA_MODE_01;
    RemakeMessageDraw(play.get(), &cursor);
    Check(!messageCancelled && play->msgCtx.ocarinaMode == OCARINA_MODE_01,
          "Y cannot cancel through the independent Message_DrawMain input path");
    Check(play->state.input[0].cur.button == BTN_B && play->state.input[0].press.button == BTN_B,
          "Music cancel filter restores gameplay button mappings after drawing");
    physical = 0; RemakeMessageDraw(play.get(), &cursor);
    Check(!messageCancelled, "Releasing Y cannot leak a cached gameplay B press");
    physical = MicRemake::Physical(0);
    play->state.input[0].cur.button = play->state.input[0].press.button = BTN_A;
    RemakeMessageDraw(play.get(), &cursor);
    Check(messageCancelled, "Nintendo B cancels through the native message path");
    messageCancelled = false; RemakeMessageDraw(play.get(), &cursor);
    Check(!messageCancelled, "Held Nintendo B does not repeat a press-only cancel");
    physical = 0; RemakeMessageDraw(play.get(), &cursor);
    play->state.input[0].press.button = BTN_B;
    RemakeMessageDraw(play.get(), &cursor);
    Check(messageCancelled, "Keyboard cancel remains available after controller release");
    messageCancelled = false; physical = MicRemake::Physical(2); mod.remakeControls = false;
    RemakeMessageDraw(play.get(), &cursor);
    Check(messageCancelled, "Disabling remake controls preserves the stock message input");
    mod.remakeControls = true; checkMessageCancel = false;
    play->state.input[0].cur.button = play->state.input[0].press.button = 0;
    physical = MicRemake::Physical(1); nativeReadButtons = BTN_B; RemakeReadInput();
    Check(input == BTN_CRIGHT, "Nintendo A plays its note instead of inheriting stock N64 B cancel");
    nativeReadButtons = BTN_A; physical = MicRemake::Physical(0); RemakeReadInput();
    Check(input == BTN_B, "Nintendo B cancels instead of playing the stock A note");
    nativeReadButtons = MicRemake::NoteMask | BTN_R | BTN_Z | BTN_B; physical = MicRemake::Physical(2);
    mod.songbook = true; RemakeReadInput();
    Check(input == 0, "Browsing songbook blocks controller note input");
    physical = 0; RemakeReadInput();
    Check(input == BTN_B, "Keyboard cancel remains available with an idle controller");
    mod.songbook = false; RemakeButtonMapping(true);
    Check(!mapping, "Remake gamepad uses the canonical native note table");
    gamepad = false; RemakeReadInput(); RemakeButtonMapping(true);
    Check(input == (MicRemake::NoteMask | BTN_R | BTN_Z | BTN_B) && mapping, "Keyboard/custom controls remain native");
    gamepad = true; save->gameMode = GAMEMODE_TITLE_SCREEN; RemakeReadInput();
    Check(input == (MicRemake::NoteMask | BTN_R | BTN_Z | BTN_B) && !UiVisible(mod, play.get()),
          "Title demo neither remaps input nor draws UI");
    save->gameMode = GAMEMODE_NORMAL;
    std::string response;
    save->inventory.items[SLOT_OCARINA] = ITEM_NONE;
    Control(&mod, "open", 4, Writer, &response);
    Check(shortcuts == 0 && response.find("need an ocarina") != std::string::npos, "Menu action never grants an unowned ocarina");
    save->inventory.items[SLOT_OCARINA] = ITEM_OCARINA_TIME; response.clear();
    Control(&mod, "open", 4, Writer, &response);
    Check(shortcuts == 1 && response == "Ocarina opened.", "Menu opens an owned ocarina through the native item service");
    // A synthetic capture state keeps recognition tests hardware-free.
    mod.capture.state.store(Capture::State::Capturing); mod.practice = true;
    mod.capture.matchSequence.store(1); mod.capture.matchedSong.store(8);
    physical = 0; Update(&mod, "", 0, Writer, &response);
    Check(submits == 0 && mod.lastMatchSequence == 1, "Practice consumes recognition without activating a song");
    mod.practice = false; mod.capture.resetRequested.store(true);
    mod.capture.matchSequence.store(2);
    response.clear(); Update(&mod, "", 0, Writer, &response);
    Check(submits == 0 && mod.lastMatchSequence == 1, "Pending reset cannot submit a stale recognized phrase");
    mod.capture.state.store(Capture::State::Idle);
    // A slow driver must not block the game, and cancel must survive late failure.
    std::atomic<bool> releaseOpen{ false }, enteredOpen{ false };
    Check(mod.capture.StartWith([&] {
        enteredOpen.store(true);
        while (!releaseOpen.load()) std::this_thread::yield();
        std::snprintf(mod.capture.lastError, sizeof(mod.capture.lastError), "test device unavailable");
        return false;
    }), "Capture starts asynchronously");
    while (!enteredOpen.load()) std::this_thread::yield();
    response.clear(); Update(&mod, "", 0, Writer, &response);
    Check(response == "audio-input-opening", "Game frame returns while a driver is still opening");
    mod.capture.Close(); releaseOpen.store(true); mod.capture.Shutdown();
    Check(mod.capture.state.load() == Capture::State::Idle, "Cancel during opening discards a late driver failure");
    // Real SDL conversion, no device: a stereo 48 kHz microphone still feeds
    // the original mono 22.05 kHz pitch detector at the correct frequency.
    mod.capture.conversion = SDL_NewAudioStream(AUDIO_S16SYS, 2, 48000, AUDIO_F32SYS, 1, kSampleRate);
    Check(mod.capture.conversion != nullptr, "Microphone format converter initializes");
    std::array<std::int16_t, 19200> nativeSamples{};
    for (int frame = 0; frame < 9600; ++frame) {
        const auto sample = static_cast<std::int16_t>(12000 * std::sin(2 * 3.141592653589793 * 880 * frame / 48000));
        nativeSamples[frame * 2] = nativeSamples[frame * 2 + 1] = sample;
    }
    mod.capture.ringWrite.store(0);
    Capture::AudioCallback(&mod.capture, reinterpret_cast<Uint8*>(nativeSamples.data()), sizeof(nativeSamples));
    Check(mod.capture.capturedSamples.load() >= kWindowSize, "Native microphone audio is converted into detector samples");
    const auto pitch = mod.capture.EstimatePitch(mod.capture.ring.data());
    Check(std::abs(pitch.hz - 880) < 8, "Rate conversion preserves the microphone pitch");
    SDL_FreeAudioStream(mod.capture.conversion); mod.capture.conversion = nullptr;
    activeMod = nullptr;
    std::puts("Remake controls, renderer state, songbook, keyboard fallback and practice/reset safety passed.");
    return EXIT_SUCCESS;
}
