local frame = 0
local function log(s) emu.print_info(s) end
local function svc() return manager.machine.ioport.ports[":SERVICE"] end
emu.register_frame_done(function()
    frame = frame + 1
    local coin = (frame >= 900 and frame < 920) and 0 or 1
    local start = (frame >= 1000 and frame < 1030) and 0 or 1
    pcall(function()
        svc().fields["Coin 1"]:set_value(coin)
        svc().fields["1 Player Start"]:set_value(start)
    end)
    if frame % 60 == 0 and frame <= 1800 then
        local sh = manager.machine.memory.shares[":workram"]
        log(string.format("frame %d credits %02X %02X %02X  e000=%02X e001=%02X", frame,
            sh:read_u8(0x2000), sh:read_u8(0x2002), sh:read_u8(0x2004),
            sh:read_u8(0x2000), sh:read_u8(0x2001)))
    end
end, "frame")
