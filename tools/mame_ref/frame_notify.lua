local n=0
local function log(s) emu.print_info("PROBE "..s) end
log("start")
emu.add_machine_frame_notifier(function()
 n=n+1
 if n<=5 then log("FRAME "..n.." time="..tostring(emu.time())) end
end)
