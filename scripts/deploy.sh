#!/bin/bash

set -euo pipefail

# RTES Deployment Script
# Usage: ./deploy.sh <environment> <version> [options]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Default values
ENVIRONMENT=""
VERSION=""
CONFIG_FILE=""
ENABLE_CANARY="true"
HEALTH_CHECK_TIMEOUT="60"
TRAFFIC_RAMP_DURATION="300"
DRY_RUN="false"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

usage() {
    cat << EOF
Usage: $0 <environment> <version> [options]

Arguments:
    environment     Target environment (dev, staging, prod)
    version         Version to deploy (e.g., v1.2.3)

Options:
    --config FILE           Custom configuration file
    --no-canary            Disable canary deployment
    --health-timeout SEC   Health check timeout (default: 60)
    --ramp-duration SEC    Traffic ramp duration (default: 300)
    --dry-run             Show what would be deployed without executing
    --help                Show this help message

Examples:
    $0 dev v1.2.3
    $0 prod v1.2.3 --no-canary --health-timeout 120
    $0 staging v1.2.3 --dry-run

EOF
}

validate_environment() {
    case "$ENVIRONMENT" in
        dev|development)
            ENVIRONMENT="development"
            CONFIG_FILE="$PROJECT_ROOT/configs/config.dev.json"
            ;;
        staging)
            ENVIRONMENT="staging"
            CONFIG_FILE="$PROJECT_ROOT/configs/config.staging.json"
            ;;
        prod|production)
            ENVIRONMENT="production"
            CONFIG_FILE="$PROJECT_ROOT/configs/config.prod.json"
            ;;
        *)
            log_error "Invalid environment: $ENVIRONMENT"
            log_error "Valid environments: dev, staging, prod"
            exit 1
            ;;
    esac
}

validate_version() {
    if [[ ! "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        log_error "Invalid version format: $VERSION"
        log_error "Expected format: vX.Y.Z (e.g., v1.2.3)"
        exit 1
    fi
}

check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check if binary exists
    if [[ ! -f "$PROJECT_ROOT/build/trading_exchange" ]]; then
        log_error "Binary not found. Please build the project first:"
        log_error "  mkdir -p build && cd build"
        log_error "  cmake .. -DCMAKE_BUILD_TYPE=Release"
        log_error "  make -j\$(nproc)"
        exit 1
    fi
    
    # Check if config file exists
    if [[ ! -f "$CONFIG_FILE" ]]; then
        log_error "Configuration file not found: $CONFIG_FILE"
        exit 1
    fi
    
    # Check encryption keys for production
    if [[ "$ENVIRONMENT" == "production" ]]; then
        if [[ -z "${RTES_ENCRYPTION_KEY_prod_key_2024:-}" ]]; then
            log_error "Production encryption key not set: RTES_ENCRYPTION_KEY_prod_key_2024"
            exit 1
        fi
    fi
    
    log_info "Prerequisites check passed"
}

run_health_checks() {
    log_info "Running pre-deployment health checks..."
    
    # Check if current instance is running
    if pgrep -f "trading_exchange" > /dev/null; then
        log_info "Current instance is running, checking health..."
        
        # Check health endpoint
        if curl -f -s "http://localhost:8080/health" > /dev/null; then
            log_info "Current instance is healthy"
        else
            log_warn "Current instance health check failed"
        fi
    else
        log_info "No current instance running"
    fi
    
    # Validate configuration
    log_info "Validating configuration..."
    if ! "$PROJECT_ROOT/build/trading_exchange" --validate-config "$CONFIG_FILE"; then
        log_error "Configuration validation failed"
        exit 1
    fi
    
    log_info "Health checks passed"
}

backup_current_version() {
    log_info "Creating backup of current version..."
    
    local backup_dir="$PROJECT_ROOT/backups/$(date +%Y%m%d_%H%M%S)"
    mkdir -p "$backup_dir"
    
    if [[ -f "$PROJECT_ROOT/build/trading_exchange" ]]; then
        cp "$PROJECT_ROOT/build/trading_exchange" "$backup_dir/"
        log_info "Backup created: $backup_dir"
    fi
}

deploy_new_version() {
    log_info "Deploying version $VERSION to $ENVIRONMENT..."
    
    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY RUN] Would deploy with the following configuration:"
        log_info "  Environment: $ENVIRONMENT"
        log_info "  Version: $VERSION"
        log_info "  Config: $CONFIG_FILE"
        log_info "  Canary: $ENABLE_CANARY"
        log_info "  Health timeout: ${HEALTH_CHECK_TIMEOUT}s"
        log_info "  Ramp duration: ${TRAFFIC_RAMP_DURATION}s"
        return 0
    fi
    
    # Set deployment environment variables
    export RTES_DEPLOYMENT_VERSION="$VERSION"
    export RTES_DEPLOYMENT_ENVIRONMENT="$ENVIRONMENT"
    export RTES_ENABLE_CANARY="$ENABLE_CANARY"
    export RTES_HEALTH_CHECK_TIMEOUT="$HEALTH_CHECK_TIMEOUT"
    export RTES_TRAFFIC_RAMP_DURATION="$TRAFFIC_RAMP_DURATION"
    
    # Start new instance with rolling update
    log_info "Starting new instance..."
    "$PROJECT_ROOT/build/trading_exchange" "$CONFIG_FILE" &
    local new_pid=$!
    
    # Wait for new instance to be ready
    log_info "Waiting for new instance to be ready..."
    local ready_timeout=30
    local ready_count=0
    
    while [[ $ready_count -lt $ready_timeout ]]; do
        if curl -f -s "http://localhost:8080/ready" | grep -q '"ready":"true"'; then
            log_info "New instance is ready"
            break
        fi
        
        sleep 1
        ((ready_count++))
        
        if [[ $ready_count -eq $ready_timeout ]]; then
            log_error "New instance failed to become ready within ${ready_timeout}s"
            kill $new_pid 2>/dev/null || true
            exit 1
        fi
    done
    
    # Monitor deployment progress
    monitor_deployment "$new_pid"
}

monitor_deployment() {
    local pid=$1
    log_info "Monitoring deployment progress..."
    
    local monitor_duration=600  # 10 minutes max
    local monitor_count=0
    
    while [[ $monitor_count -lt $monitor_duration ]]; do
        # Check if process is still running
        if ! kill -0 $pid 2>/dev/null; then
            log_error "Deployment process died unexpectedly"
            exit 1
        fi
        
        # Check deployment status
        local metrics=$(curl -s "http://localhost:8080/metrics" 2>/dev/null || echo '{}')
        local phase=$(echo "$metrics" | grep -o '"deployment_phase":"[^"]*"' | cut -d'"' -f4 || echo "unknown")
        local traffic=$(echo "$metrics" | grep -o '"traffic_percentage":"[^"]*"' | cut -d'"' -f4 || echo "0")
        
        log_info "Deployment phase: $phase, Traffic: ${traffic}%"
        
        case "$phase" in
            "full_deployment")
                log_info "Deployment completed successfully!"
                return 0
                ;;
            "failed"|"rollback")
                log_error "Deployment failed or rolled back"
                exit 1
                ;;
        esac
        
        sleep 5
        ((monitor_count += 5))
    done
    
    log_error "Deployment monitoring timed out"
    exit 1
}

rollback_deployment() {
    log_warn "Rolling back deployment..."
    
    # Kill new instance
    pkill -f "trading_exchange" || true
    
    # Restore from backup if available
    local latest_backup=$(ls -t "$PROJECT_ROOT/backups/" 2>/dev/null | head -1)
    if [[ -n "$latest_backup" && -f "$PROJECT_ROOT/backups/$latest_backup/trading_exchange" ]]; then
        log_info "Restoring from backup: $latest_backup"
        cp "$PROJECT_ROOT/backups/$latest_backup/trading_exchange" "$PROJECT_ROOT/build/"
        
        # Start restored version
        "$PROJECT_ROOT/build/trading_exchange" "$CONFIG_FILE" &
        log_info "Rollback completed"
    else
        log_error "No backup available for rollback"
        exit 1
    fi
}

cleanup() {
    log_info "Cleaning up..."
    
    # Remove old backups (keep last 5)
    if [[ -d "$PROJECT_ROOT/backups" ]]; then
        ls -t "$PROJECT_ROOT/backups" | tail -n +6 | xargs -I {} rm -rf "$PROJECT_ROOT/backups/{}" 2>/dev/null || true
    fi
}

main() {
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --config)
                CONFIG_FILE="$2"
                shift 2
                ;;
            --no-canary)
                ENABLE_CANARY="false"
                shift
                ;;
            --health-timeout)
                HEALTH_CHECK_TIMEOUT="$2"
                shift 2
                ;;
            --ramp-duration)
                TRAFFIC_RAMP_DURATION="$2"
                shift 2
                ;;
            --dry-run)
                DRY_RUN="true"
                shift
                ;;
            --help)
                usage
                exit 0
                ;;
            -*)
                log_error "Unknown option: $1"
                usage
                exit 1
                ;;
            *)
                if [[ -z "$ENVIRONMENT" ]]; then
                    ENVIRONMENT="$1"
                elif [[ -z "$VERSION" ]]; then
                    VERSION="$1"
                else
                    log_error "Too many arguments"
                    usage
                    exit 1
                fi
                shift
                ;;
        esac
    done
    
    # Validate required arguments
    if [[ -z "$ENVIRONMENT" || -z "$VERSION" ]]; then
        log_error "Missing required arguments"
        usage
        exit 1
    fi
    
    # Validate inputs
    validate_environment
    validate_version
    
    # Set config file if not provided
    if [[ -z "$CONFIG_FILE" ]]; then
        case "$ENVIRONMENT" in
            development) CONFIG_FILE="$PROJECT_ROOT/configs/config.dev.json" ;;
            staging) CONFIG_FILE="$PROJECT_ROOT/configs/config.staging.json" ;;
            production) CONFIG_FILE="$PROJECT_ROOT/configs/config.prod.json" ;;
        esac
    fi
    
    log_info "Starting deployment of $VERSION to $ENVIRONMENT"
    
    # Set up error handling
    trap rollback_deployment ERR
    
    # Execute deployment steps
    check_prerequisites
    run_health_checks
    backup_current_version
    deploy_new_version
    cleanup
    
    log_info "Deployment completed successfully!"
}

# Run main function
main "$@"