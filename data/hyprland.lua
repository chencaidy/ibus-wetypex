-- Hyprland 0.55+ window rules for WeTypeX.

hl.window_rule({
  name = "wetypex-settings",
  match = { class = "^wetypex-settings$" },
  float = true,
  size = "715 569",
  center = true,
})

hl.window_rule({
  name = "wetypex-transfer",
  match = { class = "^wetypex-transfer$" },
  float = true,
  size = "317 519",
  center = true,
})

hl.window_rule({
  name = "wetypex-ai",
  match = { class = "^wetypex-ai$" },
  float = true,
  size = "440 566",
  center = true,
})
