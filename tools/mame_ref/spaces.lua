local cpu=manager.machine.devices[":maincpu"]
local keys={}
for k,v in pairs(cpu.spaces) do keys[#keys+1]=k end
table.sort(keys)
emu.print_info("SPACES "..table.concat(keys,","))
