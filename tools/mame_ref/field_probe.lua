local frame = 0
local function log(s) emu.print_info(s) end
emu.register_frame_done(function()
    frame = frame + 1
    if frame == 30 then
        local p1 = manager.machine.ioport.ports[":P1"]
        local f = p1.fields["P1 Left"]
        log("FIELD " .. tostring(f))
        for k, v in pairs(getmetatable(f) or {}) do log("MT " .. tostring(k) .. " " .. tostring(v)) end
        local mt = getmetatable(f)
        if mt and mt.__index then
            for k, v in pairs(mt.__index) do log("METHOD " .. tostring(k) .. " " .. type(v)) end
        end
        local svc = manager.machine.ioport.ports[":SERVICE"]
        for name in pairs(svc.fields) do log("SVC " .. name) end
        manager.machine:exit()
    end
end, "frame")
