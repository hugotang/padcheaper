# pazusoba/core

The iOS solver uses the beam-search / adjacent-swap / cascade-evaluation approach
from [pazusoba/core](https://github.com/pazusoba/core), MIT licensed, copyright
2020–2021 Yiheng. The accompanying LICENSE must be retained in distributions.

References reviewed:
- Upstream commit `493dd14746eb97f275704bef73baa0f8edcf61a8`.
- Local `E:\pad\ultimate` commit `49d6b7d72c12541c327ef3e986bca110f686a593`.
  This newer branch includes working shape scoring and several upstream fixes.

`PHOrbSolver.cpp` is a small bounded adaptation for the first iOS prototype,
not a claim to include the entire Android solver or its shape profiles. It uses
one worker, explicit board lengths, fixed-size routes, capped beam/depth/deadline,
caller cancellation and error returns. It does not build the CLI, Python,
OpenMP, BMI instructions, legacy caches, or process-exit paths. Its sole objective
is ordinary three-orb combos, then erased orbs; no damage or skyfall prediction.
