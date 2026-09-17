# Turbo Chook's Haiku R1/beta6 test VM

All VM state lives in the ignored `.vm/` folder of this repository. It is
independent from Kiri's beta5 VM (VNC 5904, SSH 2224) and Summit's beta6 VM
(VNC 5905, SSH 2225).

| Item | Value |
| --- | --- |
| Guest | Haiku R1~beta6 hrev59866+79, x86_64 |
| VNC | `127.0.0.1:5906` (display `:6`) |
| SSH | `127.0.0.1:2226`, user `user`, key `.vm/id_ed25519` (`bash tools/haiku.sh`) |
| QMP | `.vm/qmp.sock` (`python3 tools/vm.py status|screenshot|click|drag|type|key`) |
| Disk | `.vm/work.qcow2`, 24 GiB, about 17 GiB free |
| Resources | 6 vCPUs, 6 GiB RAM (`TC_VM_CPUS`, `TC_VM_MEMORY`) |
| Source checkout in the guest | `/boot/home/turbochook` |
| Demo repository | `/boot/home/TurboChookDemo` with bare remote `/boot/home/TurboChookRemote.git` |
| Git installations | `/boot/system/bin/git` 2.54.0 (packaged) and `/boot/home/config/non-packaged/bin/git` 2.45.2 |

## Connecting a VNC viewer

On the development host connect to `127.0.0.1:5906`. Viewers that take a
display number can use `127.0.0.1:6`. No VNC password is set; the port only
listens on the loopback interface.

From another computer, forward the port through SSH first, then connect the
viewer to local port 5906:

```sh
ssh -N -L 5906:127.0.0.1:5906 your-user@development-host
```

The Haiku desktop is 1280 x 800. If the screen saver has blanked it, move the
pointer or run `python3 tools/vm.py key shift`.

## Everyday commands

```sh
bash tools/run-vm.sh                      # start (refuses when already running)
bash tools/haiku.sh 'uname -a'            # run a command in the guest
bash tools/build-in-vm.sh                 # sync sources, build, run tests, fetch the binary
bash tools/sync-build.sh                  # sync and build, printing compiler errors only
bash tools/haiku.sh 'cd /boot/home/turbochook && make package'
bash tools/haiku.sh 'open /boot/system/apps/TurboChook'
bash tools/haiku.sh 'hey application/x-vnd.TurboChook quit'
python3 tools/vm.py screenshot            # PNG in .vm/screen.png
bash tools/haiku.sh shutdown              # stop gracefully
```

`tools/create-demo.sh` rebuilds the demo repository: about 30 commits on
`main`, `feature/parser` (merged) and `feature/network` (unmerged), the tags
`v0.1` and `v0.2`, one stash, staged/unstaged/untracked changes and an
`origin` remote whose `main` is one commit ahead.

Haiku's `ps` prints the team name before the id, so `awk '{print $2}'` does not
give the id of a command with arguments. Use `hey <signature> quit` or
`kill $(ps | grep TurboChook | awk '{print $(NF-3)}')`.

## How it was created

1. Kiri's running beta5 VM was cloned live with QEMU's `drive-backup`
   (`tools/clone-disk.py`) into `.vm/work.qcow2`; the copy passed `qemu-img check`.
2. The clone booted with `tools/run-vm.sh`, a fresh ed25519 key replaced the
   inherited `authorized_keys`, and Kiri's leftover test data was removed.
3. `pkgman drop-repo` removed the beta5 repositories and `pkgman add-repo` added
   `https://eu.hpkg.haiku-os.org/haiku/r1beta6/x86_64/current` and
   `https://eu.hpkg.haiku-os.org/haikuports/r1beta6/x86_64/current`. Both
   commands prompt; pipe `yes yes` into them when scripting. A first attempt
   left a looping `pkgman` writing to a deleted log until the disk filled and
   BFS reported negative free space; `checkfs /boot` repaired the count.
4. `pkgman full-sync -y` upgraded every package (about 25 minutes), followed by
   `shutdown -r`. The guest now reports `R1~beta6 hrev59866+79`.
5. The beta5 `git-2.45.2-7-x86_64.hpkg` was extracted with `package extract`
   into `/boot/home/config/non-packaged/` and its `bin/git*` symlinks were
   repointed to `../lib/git-core/...`, giving the Git version selector a second
   real installation to choose from. Its exec path still names the packaged
   location, so the 2.45.2 build only offers built-in commands; that is enough
   for the demo repository.

To recreate from scratch instead, install the official beta6 x86_64 release
into a new disk, add `gcc`, `make` and `git`, create the SSH key and use the
device and port-forwarding options in `tools/run-vm.sh`.
