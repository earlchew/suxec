suxec
=====

Run an allow-listed command as another user.

#### Background

This utility allows a user to run an allow-listed program
owned by another user as that other user. Although this
helper is a setuid program, registration of an allow-listing
only requires unprivileged cooperation between parties
who trust each other.

#### Dependencies

* GNU Make
* GNU Automake
* GNU C

#### Build

* Run `autogen.sh`
* Configure using `configure`
* Build binaries using `make`
* Run tests using `make check`

#### Usage

```
usage: suxec [--] [NAME=VALUE ...] symlink

arguments:
  NAME=VALUE  Environment variables
  symlink     Command to execute
```

#### Examples

```
% suxec -- DEBUG=1 ~alice/suxec/$USER/postalert
```

#### Motivation

This utility is modelled after sudo(1), but is restricted
to running single allow-listed programs. Unlike sudo(1),
the allow-listing process does not require careful editing
a privileged file such as sudoers(5). Cooperation between
the two users to record the allow-listing is sufficient to
register the trust relationship for a specific command.

Once the registration is confirmed, following the example
of crontab(5), the environment is cleaned and populated
with LOGNAME, HOME, PATH, and SHELL. Additional
environment variables named on the command line
are added, and then execve(2) is used to run the command.

Confirmation of trust between the licensor and owner
of the program, and the licensee who will be allowed
to run the program using the identity of the owner,
occurs as follows:

* The specified command must refer to a symlink.
* The symlink must reside in a directory owned by
  the licensor, and whose name neither starts with
  a dot nor an at-sign.
* The symlink must reside in a directory whose parent
  directory is also owned by the licensor and
  has mode og=x (ie 0711).
* The symlink must owned by the licensee who is also the user
  invoking the program.
* The symlink must resolve to a file owned by the licensor and
  that the licensor has permission to execute.

If these conditions are met, setgroups(2), setgid(2), and
setuid(2) are used to drop privileges before executing the command.

#### Registration

Suppose Alice trusts Bob and wants to register him to execute her
`postalert` program on her behalf. To achieve this, Alice and Bob
cooperate to create the following directories taking note of
permissions and ownership:
```
drwx--x--x  alice alice  ~alice/suxec/
drwxr-xr-x  alice alice  ~alice/suxec/bob/
lrwxrwxrwx  bob   bob    ~alice/suxec/bob/postalert -> /home/alice/bin/postalert

drwxr-xr-x  alice alice  ~alice/bin/
-rwxr-xr-x  alice alice  ~alice/bin/postalert
```

To begin, Alice as licensor first ensures that there is a directory to
house registrations for all trusted licensees:
```
alice% mkdir -m u=rwx,og=x -p ~alice/suxec/
alice% chmod u=rwx,og=x ~alice/suxec/
```
Although other users can search the directory for a known name, the permissions
of the directory only allow the Alice as the owner to create and list
subdirectories and file.

As licensor, Alice then creates a directory to commit registrations that are
specific to Bob:
```
alice% mkdir -p ~alice/suxec/bob/
```

Alice then creates a reserved submission directory for Bob to queue requests:
```
alice% RUUID=@$(uuidgen)
alice% mkdir -m a=rwx ~alice/suxec/$RUUID/
```
Since only Alice can list the parent directory, it is practically
impossible for others to discover the name of the submission directory.

Alice trusts Bob, and shares the name of the submission directory with him.
Bob creates a symlink to the target program owned by Alice, and places that
symlink in the submission directory:
```
bob% ln -s /home/alice/bin/postalert ~alice/suxec/$RUUID/
```

Alice moves the symlink to a staging directory, and confirms
that the symlink is correct:
```
alice% SUUID=@$(uuidgen)
alice% mkdir -m u=rwx,og= ~alice/suxec/$SUUID/
alice% mv ~alice/suxec/$RUUID/postalert ~alice/suxec/$SUUID/
alice% rm -rf ~alice/suxec/$RUUID/
alice% ls -l ~alice/suxec/$SUUID/
```

Although Bob could create other content in the submission directory,
preventing Alice from deleting it, Alice has one more step
to register the request, and Bob is motivated to help
Alice clean up the submission directory.

To complete the registration, Alice completes the following actions:
* The submission directory is deleted
* The submitted symlink is resolves to the intended file owned by Alice
* The submitted symlink is owned by Bob

Once Alice is satisfied that all is correct, she commits the
symlink to the registration directory that was allocated to Bob:
```
alice% mv ~alice/suxec/$SUUID/postalert ~alice/suxec/bob/
alice% rm -rf ~alice/suxec/$SUUID/
```
