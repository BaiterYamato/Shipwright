local ship = require("ship")

-- Nunca roda: o init nativo derruba o jogo antes do entrypoint Lua.
ship.log.info("crash-demo: init nativo sobreviveu (inesperado)")
