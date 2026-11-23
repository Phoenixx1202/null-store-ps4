"""Generate a shared fingerprint from the daemon build inputs."""
import hashlib
import sys
from pathlib import Path

digest = hashlib.sha256()
for argument in sys.argv[1:]:
    source = Path(argument)
    digest.update(argument.encode() + b"\0")
    digest.update(hashlib.sha256(source.read_bytes()).digest())
build_id = digest.hexdigest()

for name, value in (
    ("build/service_build_id.h", f'#define NULLSTORE_SERVICE_BUILD "{build_id}"\n'),
    ("assets/service/build-id.txt", build_id),
):
    destination = Path(name)
    if not destination.exists() or destination.read_text() != value:
        destination.write_text(value)
