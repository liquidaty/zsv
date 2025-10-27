# Install

Download pre-built binaries and packages for macOS, Windows, Linux and BSD from
the [Releases](https://github.com/liquidaty/zsv/releases) page.

> [!IMPORTANT]
>
> For [musl libc](https://www.musl-libc.org/) static build, the dynamic
> extensions are not supported!

> [!NOTE]
>
> All package artifacts are properly
> [attested](https://github.blog/news-insights/product-news/introducing-artifact-attestations-now-in-public-beta/)
> and can be verified using [GitHub CLI](https://cli.github.com/) like this:
>
> ```shell
> gh attestation verify <downloaded-artifact> --repo liquidaty/zsv
> ```

## macOS

### macOS: Homebrew

```shell
# Update
brew update

# Install
brew install zsv

# Uninstall
brew uninstall zsv
```

### macOS: Homebrew Custom Tap

```shell
# Tap
brew tap liquidaty/zsv

# Update
brew update

# Install
brew install zsv

# Uninstall
brew uninstall zsv
```

### macOS: MacPorts

```shell
sudo port install zsv
```

## Linux

### Linux: Homebrew

```shell
# Update
brew update

# Install
brew install zsv

# Uninstall
brew uninstall zsv
```

### Linux: `apt`

```shell
# Add repository
echo "deb [trusted=yes] https://liquidaty.github.io/zsv/packages/apt/amd64/ ./" | \
  sudo tee /etc/apt/sources.list.d/zsv.list

# Update
sudo apt update

# Install CLI
sudo apt install zsv

# Install library
sudo apt install zsv-dev
```

### Linux: `rpm`

```shell
# Add repository
sudo tee /etc/yum.repos.d/zsv.repo << EOF
[zsv]
name=zsv
baseurl=https://liquidaty.github.io/zsv/packages/rpm/amd64
enabled=1
gpgcheck=0
EOF

# Install CLI
sudo yum install zsv

# Install library
sudo yum install zsv-devel
```

To install the manually downloaded `deb`/`rpm`, follow these instructions:

For Linux (Debian/Ubuntu - `*.deb`):

```shell
# Install
sudo apt install ./zsv-VERSION-amd64-linux-gcc.deb

# Uninstall
sudo apt remove zsv
```

For Linux (RHEL/CentOS - `*.rpm`):

```shell
# Install
sudo yum install ./zsv-VERSION-amd64-linux-gcc.rpm

# Uninstall
sudo yum remove zsv
```

## Windows

### Windows: `winget`

```powershell
# Install with alias
winget.exe install zsv

# Install with id
winget.exe install --id liquidaty.zsv
```

### Windows: `nuget`

Install the downloaded `.nupkg` with `nuget.exe`:

```powershell
# Install via nuget custom feed (requires absolute paths)
md nuget-feed
nuget.exe add zsv path\to\nupkg -source path\to\nuget-feed
nuget.exe install zsv -version <version> -source path\to\nuget-feed

# Uninstall
nuget.exe delete zsv <version> -source path\to\nuget-feed
```

Alternatively, install the downloaded `.nupkg` with `choco.exe`:

```powershell
# Install
choco.exe install zsv --pre -source path\to\nupkg

# Uninstall
choco.exe uninstall zsv
```

## Node

The zsv parser library is available for node:

```shell
npm install zsv-lib
```

Please note:

- This package currently only exposes a small subset of the zsv library
  capabilities. More to come!
- The CLI is not yet available as a Node package
- If you'd like to use additional parser features, or use the CLI as a Node
  package, please feel free to post a request in an issue here.

## GHCR (GitHub Container Registry)

`zsv` CLI is also available as a container image from
[Packages](https://github.com/liquidaty?tab=packages).

The container image is published on every release. In addition to the specific
release tag, the image is also tagged as `latest` i.e. `zsv:latest` always
points the latest released version.

Example:

```shell
$ docker pull ghcr.io/liquidaty/zsv
# ...
$ cat worldcitiespop_mil.csv | docker run -i ghcr.io/liquidaty/zsv count
1000000
```

For image details, see [Dockerfile](./Dockerfile). You may use this as a
baseline for your own use cases as needed.

## GitHub Actions

In a GitHub Actions workflow, you can use [`zsv/setup-action`](./setup-action)
to set up zsv+zsvlib:

```yml
- name: Set up zsv+zsvlib
  uses: liquidaty/zsv/setup-action@main
```

See [zsv/setup-action/README](./setup-action/README.md) for more details.
