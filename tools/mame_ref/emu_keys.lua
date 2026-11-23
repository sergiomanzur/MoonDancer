local keys={}
for k,v in pairs(emu) do keys[#keys+1]=k.."="..type(v) end
table.sort(keys)
emu.print_info("EMUKEYS "..table.concat(keys,","))
