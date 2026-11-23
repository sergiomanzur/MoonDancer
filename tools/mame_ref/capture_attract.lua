-- Capture reference screenshots every 2 seconds from the point the game starts drawing.
local frame = 0
local first, last, step = 600, 3800, 120
local function log(msg) emu.print_info(msg) end

emu.register_frame_done(function()
    frame = frame + 1
    if frame >= first and frame <= last and (frame - first) % step == 0 then
        manager.machine.video:snapshot()
        local cpu = manager.machine.devices[":maincpu"]
        if cpu then
            log(string.format("frame %d PC=%06X SR=%04X", frame, cpu.state["PC"].value, cpu.state["SR"].value))
        end
    end
end, "frame")
