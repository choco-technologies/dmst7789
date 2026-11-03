# Targets

dmst7789 has no hardware port: it talks to the display through the nodes of dmspi and dmgpio. A directory per CPU family only holds
`config.cmake` (the DMOD tools configuration of the family), so that the CI and the release build one package per family
(`-DDMOD_CPU_FAMILY=<family>`).
