local frame = 0
emu.register_frame_done(function()
    frame = frame + 1
    if frame == 60 then
        local scr = manager.machine.screens[":screen"]
        emu.print_info(string.format("SCREEN %dx%d frame=%d", scr.width, scr.height, scr:frame_number()))
        local ok, err = pcall(function()
            emu.print_info(string.format("PIXEL %08X", scr:pixel(100, 100)))
        end)
        emu.print_info("pixel ok=" .. tostring(ok) .. " err=" .. tostring(err))
        manager.machine:exit()
    end
end, "frame")
