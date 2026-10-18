YM2151 fast backend for ZZVGMPlay A0r4f.

This is the same Jarek Burczynski / FBNeo-derived YM2151 core already validated
in the ZZRastan Core1 project. The source used here comes from the validated
ZZRastan bare-metal copy (static one-chip storage, savestate code removed).

ZZVGM modification: YM2151Shutdown() does not free YMPSG because this bare-metal
copy uses static storage. It only clears the logical pointer, allowing a later
re-init with a different clock/sample rate.
