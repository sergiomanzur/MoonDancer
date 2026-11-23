local n=0
local function log(s) emu.print_info("PROBE "..s) end
emu.register_periodic(function()
 n=n+1
 if n<=8 then log("PER "..n.." time="..tostring(emu.time())) end
end)
