# IR provenance manifest

No third-party impulse responses are distributed by this repository or by
the installer package built with `./make_installer.sh` (without `--with-irs`).

Septima is designed around Samplicity's free Bricasti M7 library, which each
user downloads from <https://samplicity.com/downloads/> and points Septima at
(see README). That library may not be redistributed.

Milestone 2 still distributes no third-party impulse responses. The plugin's
**Load WAV IR** control accepts external user files and keeps them outside the
application content. Imported metadata records the path, source sample rate,
channel count, duration, and an explicit `user-import only` redistribution
status; it does not infer a licence or provenance claim.

For every future bundled or optional response, add:

| Field | Required value |
| --- | --- |
| Source URL | Stable URL for the original provider |
| Creator | Original creator or institution |
| Original filename | Exact source filename |
| Licence | Name and version, with a copy retained in release records |
| Commercial use | Explicit yes/no and evidence |
| Modification | Explicit yes/no and evidence |
| Redistribution | Explicit yes/no for compiled plugin distribution |
| Attribution | Exact required notice |
| Included status | Factory, optional licensed, or user-import only |

Potential sources listed in the development brief are research candidates only.
A download page or “free” label is not sufficient evidence of redistribution
rights. Until those rights are verified, the supported workflow is external
user import through the WAV reader.
