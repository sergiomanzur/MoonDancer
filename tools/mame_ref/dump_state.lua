local frame = 0
local checks = { 130, 200, 300, 400, 500, 600, 700 }
local want = {}
for _, f in ipairs(checks) do want[f] = true end
local function log(msg) emu.print_info(msg) end

emu.register_frame_done(function()
    frame = frame + 1
    if not want[frame] then return end
    local cpu = manager.machine.devices[":maincpu"]
    local mcu = manager.machine.devices[":mcu"]
    log(string.format("frame %d main PC=%06X SR=%04X D0=%08X A7=%08X mcu PC=%04X PSW=%02X P0=%02X P1=%02X P2=%02X P3=%02X",
        frame, cpu.state["PC"].value, cpu.state["SR"].value, cpu.state["D0"].value, cpu.state["SP"].value,
        mcu.state["PC"].value, mcu.state["PSW"].value, mcu.state["P0"].value, mcu.state["P1"].value,
        mcu.state["P2"].value, mcu.state["P3"].value))
    local mem = cpu.spaces["program"]
    local s = {}
    for i = 0, 15 do
        s[#s + 1] = string.format("%04X", mem:read_u16(0xFFC000 + i * 2))
    end
    log("  workram: " .. table.concat(s, " "))
end, "frame")
