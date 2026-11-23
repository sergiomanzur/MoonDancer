local frame = 0
local function log(s) emu.print_info(s) end
local svc = manager.machine.ioport.ports[":SERVICE"]
local p1 = manager.machine.ioport.ports[":P1"]
local coin = svc.fields["Coin 1"]
local start = svc.fields["1 Player Start"]
local left = p1.fields["P1 Left"]
emu.register_frame_done(function()
    frame = frame + 1
    if frame >= 500 and frame < 1500 then coin:set_value(1) else coin:set_value(0) end
    if frame >= 2000 and frame < 2400 then start:set_value(1) else start:set_value(0) end
    if frame >= 3000 and frame < 3100 then left:set_value(1) else left:set_value(0) end
    if (frame >= 500 and frame <= 520) or frame % 250 == 0 then
        log(string.format("PORT %d svc=%02X p1=%02X", frame, svc:read(), p1:read()))
    end
    if frame == 3000 or frame == 5000 then
        manager.machine.screens[":screen"]:pixel(0,0)
        local scr = manager.machine.screens[":screen"]
        local function save_bmp(path)
            local w, h = scr.width, scr.height
            local img = {}
            for y = h - 1, 0, -1 do
                local line = {}
                for x = 0, w - 1 do
                    local p = scr:pixel(x, y)
                    line[#line + 1] = string.char(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF)
                end
                img[#img + 1] = table.concat(line)
            end
            local data = table.concat(img)
            local size = 54 + #data
            local f = assert(io.open(path, "wb"))
            f:write("BM", string.char(size & 0xFF, (size >> 8) & 0xFF, (size >> 16) & 0xFF, (size >> 24) & 0xFF,
                0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0))
            f:write(string.char(w & 0xFF, (w >> 8) & 0xFF, 0, 0, h & 0xFF, (h >> 8) & 0xFF, 0, 0))
            f:write(string.char(1, 0, 24, 0), string.rep("\0", 24), data)
            f:close()
        end
        save_bmp(string.format("build/iptest_%d.bmp", frame))
    end
    if frame >= 1200 then
        log("DONE")
        manager.machine:exit()
    end
end, "frame")
