# Built-in Unreal trees: open the Procedural Vegetation Editor sample trees, photograph them, and judge them against UK photos (M6)

## Owner's words
11 Oct, on the scripted vegetation kit: "that vegetation is super low quality assets, thats the main issue"
11 Oct, to the previous session, relayed to this one and confirmed here: "ok maybe we just go for the built in ones"
11 Oct, in this chat, about the account-gated sources (Quixel Megaplants and Megascans Trees on Fab): "well we would come to the accounts as a fallback"
11 Oct: "i will never open it, this takes too long. just mess around with it yourself to test it (ideally in an agent)"
11 Oct: "it isn't even getting relevant ref images!!!"
24 Oct 2026: "How was vegetation getting on btw this is what will make it look good"
24 Oct 2026, asked whether an agent may export from the PVE editor by driving the window with real mouse and keyboard input: "Yes, and I'm away now"

## Scope
This round (pvetrees1) judges the Unreal 5.8 Procedural Vegetation Editor sample trees for Case A only: a hedgerow broadleaf tree seen from a lane, driver's eye 20-60 m. The verdict decides whether account-gated sources (Quixel Megaplants, Megascans Trees) are needed for this kind of tree. Later rounds take Cases B, C and D, one per round (contract item 22).

## Cases
"The built in ones" are the sample trees shipped with Unreal 5.8's experimental Procedural Vegetation Editor (PVE), on disk at C:\UE_5.8\Engine\Plugins\Experimental\ProceduralVegetationEditor\Content\SampleAssets (about 550 MB; top-level folders checked 24 Oct: Maps, Materials, PCG, StarterContent, Tree_Common_Hazel_01, Tree_European_Beech_01, Tree_European_QuakingAspen_01, Tree_Norway_Maple_01, WindSettings; presets Leaf_Tree_01-04, Pine_01, Spruce_01-02, Shrub_01, Sapling_01, Deciduous_Shrub_01 sit under StarterContent; map Maps/Asset_Zoo.umap). Nobody has looked at them: they are .uasset only. This job decides, with pictures, whether they can carry the game's trees, hedges and shrubs, so the owner need not set up accounts. The finish line for Case A is "matches the photo" at the photo's distance and angle; where it does not, say exactly what is wrong. Stop rule: the self-audit residual holds only unfixable items, or 3 passes; no hour limit.

Case A, hedgerow broadleaf tree seen from a lane (driver's eye, 20-60 m):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\66_veg-hedgerow-oak-chilbrook-4942847.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\67_veg-hedgerow-oak-3966899.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\68_veg-hedgerow-oak-crouch-lane-3052312.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\80_veg-good-easter-hedgerow-oak.jpg

Also open, all of them:
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\INDEX.md
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\research\free-assets-vegetation.md (what the free downloads gave; its composites are in docs\reference\_compare\free-assets-veg\; the Mantissa birch and Poly Haven conifers already on disk are the things the PVE trees must beat or complement)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\unreal-architecture.md
- C:\Users\bgbg0\Documents\Programming\roadworld\unreal\tools\README.md
A pair is valid only if photo and render show the same kind of plant, at the same distance and angle. A near miss is never paired.

What to produce, per sample species and per preset (every one of them; say which you could not open), for Case A:
1. A neutral turnaround picture set in summer daylight: whole plant at 30 m, at 10 m, and a close view of bark and leaves at 2 m, with a 1.8 m figure or a scale bar. JPEG.
2. For each case, a render framed like its best photo and a composite (render | photo). In case C try to make a 1.5-2 m flailed hedge run of 30 m from the shrub/hazel presets by whatever in-engine means PVE gives (parameters, instancing, scaling); if it cannot look like the photos, say so and show the nearest attempt.
3. Facts, each from C:\UE_5.8 source or by measuring in the editor, never from memory: triangle and material counts per tree; whether the export is a static mesh, Nanite, Nanite Foliage; what `r.Nanite.Foliage` does, its default, whether the project can turn it on and what that costs; whether Dynamic Wind works; how many seeds/variants one preset gives and how a variant is generated from code or a commandlet (the game must place trees itself: nothing hand-dressed); frame time with 1, 500 and 5000 instances in view at 1080p on this machine (RTX 4060); disk size of an exported tree; the licence text that covers this sample content (quote the file).
4. A verdict table: species or preset | UK look (which UK species it can pass for: oak, ash, sycamore, field maple, hawthorn, hazel, birch, Scots pine, spruce) | good at close / mid / far | cost | use it for | not good enough for. Then one plain list for the owner: what the built-in trees cover, and what still has no good source (so the account-gated fallback list is as short as possible).

Start: worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt\pvetrees1, branch agent/pvetrees1, cut from current main. This is an evaluation: do not add trees to the game's sandbox, do not change the game module, the plugin or sim_core. If you need the PVE plugin enabled, prefer a throwaway blank project in C:\Users\bgbg0\Documents\Programming\roadworld-scratch\pvetrees1 (no C++ build needed) over changing CityPainter.uproject; say which you did. Any exported sample you want to keep goes in the scratch folder, not in git (engine content is not committed). Pictures and composites: renders in the progress folder as JPEG; composites that contain reference photos go only in C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\_compare\pvetrees1\ (private).
UNREAL LANE (one Unreal build, editor or -game process at a time on this machine, several agents share it): before any UBT build, editor or game launch, take the lock by creating the directory C:\Users\bgbg0\Documents\Programming\roadworld\.claude\state\unreal_lane.lock (mkdir fails if it exists: then wait and retry every 60 s; also wait while any UnrealEditor, UnrealBuildTool or dotnet UBT process that you did not start is running), write a file owner.txt inside with your branch name and the time, and remove the directory as soon as your build or run ends, every time, including on failure. Never hold it while you are only editing or thinking. If it is older than 60 minutes and no Unreal process runs, say so in STATUS.md, then take it. Other rounds may compile sim_core in other worktrees and the machine has 15 GB RAM: close the editor when you are not using it, and never kill a process you did not start. The mcp blender connection on this machine is unreliable (it returned a CAD scene once): do not use it.

## Faults
1. Nobody has rendered the PVE sample trees or compared them with UK hedgerow photos.

## Launched
- pvetrees2 asset evaluation: Case A, send-back round after the pvetrees1 audit (export all presets and species variants, hedge attempt, frame times, licence). It takes the Unreal lane through the lane lock between other rounds' runs.

## Waiting
- Cases B, C, D: one case per round, after pvetrees1's verdict.
- Importing any tree into the game sandbox and instancing along roads (queue items 6 and 7): waits for the verdict. Account-gated vegetation (Megaplants, Megascans Trees): fallback only, after the verdict.

## Later rounds
Kept word for word from the first version of this file; each is its own round.

Case B, roadside tree belt beside a dual carriageway or roundabout (driver's eye, mid distance):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\84_dual-a617-hasland-4205264.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\85_dual-farnham-bypass-6246401.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\86_dual-a38-menheniot-4163196.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\88_roundabout-trees-4680554.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\87_roundabout-trees-6900665.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\89_roundabout-trees-7086287.jpg
Case C, lane hedge (hawthorn, flailed) built from the PVE shrub and hazel presets (driver's eye, 5-30 m):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\60_veg-hawthorn-hedgerow-399259.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\61_veg-flailing-holton-1172122.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\62_veg-flailed-hedge-torquay-8212171.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\69_veg-hedgerow-santon-lane-kent.jpg
Case D, conifer plantation edge (mid and far):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\81_veg-track-beside-conifer-plantation-5389871.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\82_veg-plantation-dalairn-227163.jpg
- Case D has only 2 photos here: fetch at least one more matching conifer-edge photo (reference photo downloads are pre-approved) and log it in the street INDEX.
