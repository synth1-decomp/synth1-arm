# MATCH-HACKs

Four functions contain code that exists only to reproduce the original machine code, and one file
uses a `#line` directive to keep a line number that is compiled into the binary. Each spot is
marked `// MATCH-HACK:` in the source. Natural rewrites of each were searched for at length, and
all of them come out different with clang-902.0.39.2 at `-Os`. The original was most likely built
with slightly different compiler input; the reasons are below.

| Function | File | Hack | Why a natural form doesn't match |
|---|---|---|---|
| `CGtAmpSim1::CGtAmpSim1` (warm-up loop) | `src/techpop/distortion.cpp` | a dead `if (i < 0) break;` | IndVarSimplify's LFTR rewrites the exit test of the plain `for (i = 0; i < 600; i++)` loop. The original keeps `i < 599` on an up-counter, exactly as with `-mllvm -disable-lftr`. A second exiting block is the only source-level way to skip LFTR. |
| `CVcf::calcSetAmount` | `src/techpop/vcf.cpp` | `else __asm__("");` | The original's clamp reaches instruction selection as a branch with an empty arm. Every natural clamp is folded into a select by SimplifyCFG, and X86 then lowers it to `maxss`. CodeGenPrepare's select-to-branch is disabled at `-Os`. |
| `CVcf::lowpassDiodeLadder` | `src/techpop/vcf.cpp` | an empty `asm` barrier after each of the five state copies | The original copies `m_z[0..4]` as five ascending 8-byte moves. Natural copies become a `memcpy` or SLP-vectorized pairs. |
| `CSynthesizer1::convValIntFloat` | `src/techpop/synth1.cpp` | `__builtin_assume` forwarder cases, plus dummy `bToFloat` switches folded with `__builtin_expect` | The original's block layout needs one jump-table branch's probability just above 3/8. A plain `if (bToFloat)` gives exactly 3/8, and no natural construct gives anything in between. The hack produces 0.37556 through counted SimplifyCFG weights. |
| `-[Synth1CocoaViewFactory uiViewForAudioUnit:withSize:]` | `src/audiounit/Synth1CocoaViewFactory.mm` | `#line 393` directly before `assert(result == noErr);` | The assert's failure message embeds `__LINE__`, and the released binary has 393. The directive keeps that value however much documentation precedes the assert. |

There is also one build-process hack, the relink that reproduces the GUI bundle's LC_UUID.
See `HOW_IT_WORKS.md`, section 4.
