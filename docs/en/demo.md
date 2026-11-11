# Demo (asciinema)

The lab can be recorded as an [asciinema](https://asciinema.org) cast.  Every
byte in the recording is the real output of the lab VMs (Alpine under libvirt
running the kernel built from source with `pf.ko`); only the pacing is
scripted.

## Record

```sh
pip install asciinema            # once
bash scripts/vm-run-tests.sh      # or: scripts/vm-create.sh + deploy
asciinema rec out/demo-pf-linux.cast -c "bash scripts/demo-session.sh"
```

The session walks through:

1. the lab topology and the freshly loaded `pf.ko` (`dmesg` proof),
2. `pfctl -s info` over the OpenBSD ioctl ABI,
3. a real `pf.conf` (table, stateful rules, explicit drop) and `pfctl -sr`,
4. transit ping + `pfctl -ss` (stateful),
5. `pfctl -t <table> -T show` (live tables),
6. a blocked ping (counters with `pfctl -sr -vv`),
7. `divert-to` with the `pdivert` relay (packet to userspace and back),
8. shutdown.

## Play back

```sh
asciinema play out/demo-pf-linux.cast
```

## Upload

```sh
asciinema upload out/demo-pf-linux.cast
```

`scripts/demo-record.py` is an alternative recorder that produces the same
asciicast v2 format without the asciinema binary (it drives the lab over
ssh and writes the cast itself).
