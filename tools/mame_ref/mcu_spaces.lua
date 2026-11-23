local mcu=manager.machine.devices[":mcu"]
local keys={}
for k,v in pairs(mcu.spaces) do keys[#keys+1]=k end
table.sort(keys)
emu.print_info("MCUSPACES "..table.concat(keys,","))
