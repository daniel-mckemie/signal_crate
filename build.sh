#!/bin/bash

# Signal Crate Complete Installation and Build Script
# This script installs all dependencies and builds the entire Signal Crate application

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function to detect OS
detect_os() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo "linux"
    else
        echo "unknown"
    fi
}

# Function to install dependencies on macOS
install_macos_deps() {
    print_status "Installing dependencies for macOS..."

    # Check if Homebrew is installed
    if ! command_exists brew; then
        print_status "Homebrew not found. Installing Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

        # Add Homebrew to PATH for Apple Silicon Macs
        if [[ $(uname -m) == "arm64" ]]; then
            echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
            eval "$(/opt/homebrew/bin/brew shellenv)"
        fi
    else
        print_success "Homebrew is already installed"
    fi

    # Update Homebrew
    print_status "Updating Homebrew..."
    brew update

    # Install required dependencies
    print_status "Installing Signal Crate dependencies..."
    local deps=("portaudio" "portmidi" "liblo" "fftw" "libsndfile" "ncurses" "pkg-config")

    for dep in "${deps[@]}"; do
        if brew list "$dep" &>/dev/null; then
            print_success "$dep is already installed"
        else
            print_status "Installing $dep..."
            brew install "$dep"
        fi
    done
}

# Function to install dependencies on Linux
install_linux_deps() {
    print_status "Installing dependencies for Linux..."

    # Detect Linux distribution
    if command_exists apt-get; then
        # Debian/Ubuntu
        print_status "Detected Debian/Ubuntu system"
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            pkg-config \
            libportaudio2 \
            libportaudio-dev \
            libportmidi0 \
            libportmidi-dev \
            liblo7 \
            liblo-dev \
            libfftw3-3 \
            libfftw3-dev \
            libsndfile1 \
            libsndfile1-dev \
            libncurses5-dev \
            libncursesw5-dev
    elif command_exists yum; then
        # RHEL/CentOS/Fedora
        print_status "Detected RHEL/CentOS/Fedora system"
        sudo yum groupinstall -y "Development Tools"
        sudo yum install -y \
            pkg-config \
            portaudio-devel \
            portmidi-devel \
            liblo-devel \
            fftw-devel \
            libsndfile-devel \
            ncurses-devel
    elif command_exists pacman; then
        # Arch Linux
        print_status "Detected Arch Linux system"
        sudo pacman -Syu --needed \
            base-devel \
            pkg-config \
            portaudio \
            portmidi \
            liblo \
            fftw \
            libsndfile \
            ncurses
    else
        print_error "Unsupported Linux distribution. Please install dependencies manually:"
        print_error "Required packages: portaudio, portmidi, liblo, fftw, libsndfile, ncurses, pkg-config"
        exit 1
    fi
}

# Function to verify dependencies
verify_dependencies() {
    print_status "Verifying dependencies..."

    local deps=("pkg-config")
    local missing_deps=()

    for dep in "${deps[@]}"; do
        if ! command_exists "$dep"; then
            missing_deps+=("$dep")
        fi
    done

    # Check for library headers using pkg-config
    local lib_deps=("portaudio-2.0" "portmidi" "liblo" "fftw3" "sndfile")
    for lib in "${lib_deps[@]}"; do
        if ! pkg-config --exists "$lib" 2>/dev/null; then
            # Try alternative names
            case "$lib" in
                "portaudio-2.0")
                    if ! pkg-config --exists "portaudio" 2>/dev/null; then
                        missing_deps+=("portaudio")
                    fi
                    ;;
                "sndfile")
                    if ! pkg-config --exists "libsndfile" 2>/dev/null; then
                        missing_deps+=("libsndfile")
                    fi
                    ;;
                *)
                    missing_deps+=("$lib")
                    ;;
            esac
        fi
    done

    if [ ${#missing_deps[@]} -eq 0 ]; then
        print_success "All dependencies are installed"
        return 0
    else
        print_error "Missing dependencies: ${missing_deps[*]}"
        return 1
    fi
}

# Function to create necessary directories
create_directories() {
    print_status "Creating necessary directories..."

    # Create modules directory if it doesn't exist
    if [ ! -d "modules" ]; then
        mkdir -p modules
        print_status "Created modules directory"
    fi

    # Create output directory for environment modules
    if [ ! -d "e_output_files" ]; then
        mkdir -p e_output_files
        print_status "Created e_output_files directory"
    fi
}

# Function to build the application
build_application() {
    print_status "Building Signal Crate..."

    # Clean previous build
    print_status "Cleaning previous build..."
    if [ -f "Makefile" ]; then
        make clean 2>/dev/null || true
    fi

    # Build everything
    print_status "Building main application and all modules..."
    make all

    # Verify the build was successful
    if [ -f "SignalCrate" ]; then
        print_success "SignalCrate executable created successfully"
    else
        print_error "Build failed - SignalCrate executable not found"
        exit 1
    fi

    # Make the executable... executable
    chmod +x SignalCrate
}

# Function to setup shell alias
setup_shell_alias() {
    print_status "Setting up shell alias..."

    local signal_crate_dir="$(pwd)"
    local shell_rc=""

    # Detect shell and set appropriate RC file
    if [[ "$SHELL" == *"zsh"* ]]; then
        shell_rc="$HOME/.zshrc"
    elif [[ "$SHELL" == *"bash"* ]]; then
        shell_rc="$HOME/.bashrc"
    else
        print_warning "Unknown shell. Please manually add the alias to your shell configuration."
        return
    fi

    # Check if alias already exists
    if grep -q "# Signal Crate alias" "$shell_rc" 2>/dev/null; then
        print_warning "Signal Crate alias already exists in $shell_rc"
        return
    fi

    # Add alias to shell RC file
    cat >> "$shell_rc" << EOF

# Signal Crate alias
export SIGNAL_CRATE_DIR="$signal_crate_dir"
export SIGNAL_CRATE_BIN="\$SIGNAL_CRATE_DIR/SignalCrate"

sig() {
    local arg="\$1"

    cd "\$SIGNAL_CRATE_DIR" || return 1

    if [ -n "\$arg" ]; then
        "\$SIGNAL_CRATE_BIN" "\$arg"
    else
        "\$SIGNAL_CRATE_BIN"
    fi
}
EOF

    print_success "Added 'sig' alias to $shell_rc"
    print_status "Run 'source $shell_rc' or restart your terminal to use the 'sig' command"
}

# Function to run basic tests
run_tests() {
    print_status "Running basic tests..."

    # Test if the executable runs and shows help/version
    if ./SignalCrate --help >/dev/null 2>&1 || ./SignalCrate -h >/dev/null 2>&1; then
        print_success "SignalCrate executable responds to help flag"
    else
        print_warning "SignalCrate executable doesn't respond to help flag (this may be normal)"
    fi

    # Check if executable is properly linked
    if ldd ./SignalCrate >/dev/null 2>&1 || otool -L ./SignalCrate >/dev/null 2>&1; then
        print_success "SignalCrate executable is properly linked"
    else
        print_warning "Could not verify library linking"
    fi
}

# Function to display final instructions
display_final_instructions() {
    echo ""
    print_success "Signal Crate installation and build complete!"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    print_status "How to run Signal Crate:"
    echo "  1. From this directory: ./SignalCrate"
    echo "  2. With a patch file: ./SignalCrate mypatch.txt"
    echo "  3. Using the 'sig' alias (after sourcing your shell config):"
    echo "     - sig"
    echo "     - sig mypatch.txt"
    echo ""
    print_status "To enable the 'sig' alias in your current session:"
    if [[ "$SHELL" == *"zsh"* ]]; then
        echo "  source ~/.zshrc"
    elif [[ "$SHELL" == *"bash"* ]]; then
        echo "  source ~/.bashrc"
    else
        echo "  source your shell configuration file"
    fi
    echo ""
    print_status "Example patch to test:"
    echo "  vco as vco1"
    echo "  vco as vco2"
    echo "  moog_filter(vco1,vco2) as out"
    echo ""
    print_status "For more information, see README.md"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

# Main installation function
main() {
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "                    Signal Crate Installation Script"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""

    # Detect operating system
    local os=$(detect_os)
    print_status "Detected OS: $os"

    # Install dependencies based on OS
    case "$os" in
        "macos")
            install_macos_deps
            ;;
        "linux")
            install_linux_deps
            ;;
        *)
            print_error "Unsupported operating system: $OSTYPE"
            print_error "Please install dependencies manually and run 'make all'"
            exit 1
            ;;
    esac

    # Verify dependencies are installed
    if ! verify_dependencies; then
        print_error "Dependency verification failed. Please check the installation."
        exit 1
    fi

    # Create necessary directories
    create_directories

    # Build the application
    build_application

    # Setup shell alias
    setup_shell_alias

    # Run basic tests
    run_tests

    # Display final instructions
    display_final_instructions
}

# Check if script is being run from the correct directory
if [ ! -f "Makefile" ] || [ ! -f "main.c" ]; then
    print_error "This script must be run from the Signal Crate root directory"
    print_error "Make sure you're in the directory containing Makefile and main.c"
    exit 1
fi

# Run main installation
main "$@"

