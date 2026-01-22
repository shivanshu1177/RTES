#!/bin/bash

set -euo pipefail

# RTES Production Deployment Script
# Usage: ./production_deploy.sh <version> [options]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Configuration
VERSION=""
DEPLOYMENT_TYPE="blue_green"
ENABLE_CHAOS_TESTING="false"
SKIP_LOAD_TESTING="false"
ROLLBACK_ON_FAILURE="true"
BACKUP_BEFORE_DEPLOY="true"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step() { echo -e "${BLUE}[STEP]${NC} $1"; }

usage() {
    cat << EOF
Usage: $0 <version> [options]

Arguments:
    version             Version to deploy (e.g., v1.2.3)

Options:
    --deployment-type TYPE    Deployment type: blue_green, canary, rolling (default: blue_green)
    --enable-chaos           Enable chaos engineering during deployment
    --skip-load-test         Skip load testing validation
    --no-rollback           Disable automatic rollback on failure
    --no-backup             Skip backup creation before deployment
    --help                  Show this help message

Examples:
    $0 v1.2.3
    $0 v1.2.3 --deployment-type canary --enable-chaos
    $0 v1.2.3 --skip-load-test --no-backup

EOF
}

validate_prerequisites() {
    log_step "Validating deployment prerequisites"
    
    # Check if binary exists
    if [[ ! -f "$PROJECT_ROOT/build/trading_exchange" ]]; then
        log_error "Binary not found. Build the project first."
        exit 1
    fi
    
    # Check production configuration
    if [[ ! -f "$PROJECT_ROOT/configs/config.prod.json" ]]; then
        log_error "Production configuration not found"
        exit 1
    fi
    
    # Check encryption keys
    if [[ -z "${RTES_ENCRYPTION_KEY_prod_key_2024:-}" ]]; then
        log_error "Production encryption key not set"
        exit 1
    fi
    
    # Check TLS certificates
    if [[ ! -f "/etc/rtes/certs/server.crt" ]] || [[ ! -f "/etc/rtes/certs/server.key" ]]; then
        log_error "TLS certificates not found in /etc/rtes/certs/"
        exit 1
    fi
    
    log_info "Prerequisites validation passed"
}

run_production_readiness_checks() {
    log_step "Running production readiness validation"
    
    # Run the production readiness validator
    if ! "$PROJECT_ROOT/build/trading_exchange" --validate-production-readiness; then
        log_error "Production readiness validation failed"
        return 1
    fi
    
    log_info "Production readiness validation passed"
}

create_backup() {
    if [[ "$BACKUP_BEFORE_DEPLOY" == "true" ]]; then
        log_step "Creating system backup"
        
        local backup_dir="/var/backups/rtes/$(date +%Y%m%d_%H%M%S)"
        mkdir -p "$backup_dir"
        
        # Backup configuration
        cp -r "$PROJECT_ROOT/configs" "$backup_dir/"
        
        # Backup current binary
        if [[ -f "/opt/rtes/trading_exchange" ]]; then
            cp "/opt/rtes/trading_exchange" "$backup_dir/"
        fi
        
        # Backup system state
        if ! "$PROJECT_ROOT/build/trading_exchange" --create-backup "$backup_dir"; then
            log_error "Backup creation failed"
            return 1
        fi
        
        log_info "Backup created: $backup_dir"
        echo "$backup_dir" > /tmp/rtes_last_backup
    fi
}

run_chaos_engineering_tests() {
    if [[ "$ENABLE_CHAOS_TESTING" == "true" ]]; then
        log_step "Running chaos engineering tests"
        
        # Run chaos experiments
        if ! "$PROJECT_ROOT/build/trading_exchange" --run-chaos-experiments; then
            log_error "Chaos engineering tests failed"
            return 1
        fi
        
        log_info "Chaos engineering tests passed"
    fi
}

run_load_testing() {
    if [[ "$SKIP_LOAD_TESTING" == "false" ]]; then
        log_step "Running load testing validation"
        
        # Start load test
        local load_test_config="{
            \"concurrent_clients\": 500,
            \"orders_per_second\": 100000,
            \"duration\": 300,
            \"enable_chaos\": $ENABLE_CHAOS_TESTING
        }"
        
        echo "$load_test_config" > /tmp/load_test_config.json
        
        if ! "$PROJECT_ROOT/build/trading_exchange" --run-load-test /tmp/load_test_config.json; then
            log_error "Load testing failed"
            return 1
        fi
        
        log_info "Load testing passed"
    fi
}

deploy_blue_green() {
    log_step "Executing blue-green deployment"
    
    # Determine target slot
    local current_slot
    current_slot=$("$PROJECT_ROOT/build/trading_exchange" --get-active-slot 2>/dev/null || echo "blue")
    local target_slot
    if [[ "$current_slot" == "blue" ]]; then
        target_slot="green"
    else
        target_slot="blue"
    fi
    
    log_info "Current slot: $current_slot, Target slot: $target_slot"
    
    # Prepare deployment
    log_info "Preparing $target_slot slot with version $VERSION"
    if ! "$PROJECT_ROOT/build/trading_exchange" --prepare-deployment "$target_slot" "$VERSION"; then
        log_error "Deployment preparation failed"
        return 1
    fi
    
    # Health check new slot
    log_info "Validating $target_slot slot health"
    sleep 30  # Allow startup time
    
    if ! "$PROJECT_ROOT/build/trading_exchange" --validate-slot-health "$target_slot"; then
        log_error "Health check failed for $target_slot slot"
        return 1
    fi
    
    # Switch traffic
    log_info "Switching traffic to $target_slot slot"
    if ! "$PROJECT_ROOT/build/trading_exchange" --switch-traffic "$target_slot"; then
        log_error "Traffic switch failed"
        return 1
    fi
    
    # Final validation
    sleep 10
    if ! "$PROJECT_ROOT/build/trading_exchange" --validate-slot-health "$target_slot"; then
        log_error "Post-switch health check failed"
        if [[ "$ROLLBACK_ON_FAILURE" == "true" ]]; then
            rollback_deployment
        fi
        return 1
    fi
    
    log_info "Blue-green deployment completed successfully"
}

deploy_canary() {
    log_step "Executing canary deployment"
    
    # Start canary deployment
    if ! "$PROJECT_ROOT/build/trading_exchange" --start-canary-deployment "$VERSION"; then
        log_error "Canary deployment failed"
        return 1
    fi
    
    # Monitor canary for 10 minutes
    log_info "Monitoring canary deployment for 10 minutes"
    for i in {1..60}; do
        if ! "$PROJECT_ROOT/build/trading_exchange" --check-canary-health; then
            log_error "Canary health check failed"
            if [[ "$ROLLBACK_ON_FAILURE" == "true" ]]; then
                rollback_deployment
            fi
            return 1
        fi
        sleep 10
    done
    
    # Promote canary to full deployment
    log_info "Promoting canary to full deployment"
    if ! "$PROJECT_ROOT/build/trading_exchange" --promote-canary; then
        log_error "Canary promotion failed"
        return 1
    fi
    
    log_info "Canary deployment completed successfully"
}

deploy_rolling() {
    log_step "Executing rolling deployment"
    
    # Start rolling deployment
    if ! "$PROJECT_ROOT/build/trading_exchange" --start-rolling-deployment "$VERSION"; then
        log_error "Rolling deployment failed"
        return 1
    fi
    
    # Monitor rolling deployment
    while "$PROJECT_ROOT/build/trading_exchange" --is-deployment-active; do
        local phase
        phase=$("$PROJECT_ROOT/build/trading_exchange" --get-deployment-phase)
        log_info "Deployment phase: $phase"
        
        if [[ "$phase" == "failed" ]]; then
            log_error "Rolling deployment failed"
            if [[ "$ROLLBACK_ON_FAILURE" == "true" ]]; then
                rollback_deployment
            fi
            return 1
        fi
        
        sleep 5
    done
    
    log_info "Rolling deployment completed successfully"
}

rollback_deployment() {
    log_warn "Initiating deployment rollback"
    
    case "$DEPLOYMENT_TYPE" in
        "blue_green")
            "$PROJECT_ROOT/build/trading_exchange" --rollback-blue-green || true
            ;;
        "canary")
            "$PROJECT_ROOT/build/trading_exchange" --rollback-canary || true
            ;;
        "rolling")
            "$PROJECT_ROOT/build/trading_exchange" --rollback-rolling || true
            ;;
    esac
    
    # Restore from backup if available
    if [[ -f "/tmp/rtes_last_backup" ]]; then
        local backup_dir
        backup_dir=$(cat /tmp/rtes_last_backup)
        if [[ -d "$backup_dir" ]]; then
            log_info "Restoring from backup: $backup_dir"
            "$PROJECT_ROOT/build/trading_exchange" --restore-from-backup "$backup_dir" || true
        fi
    fi
    
    log_warn "Rollback completed"
}

post_deployment_validation() {
    log_step "Running post-deployment validation"
    
    # Wait for system to stabilize
    sleep 30
    
    # Health checks
    if ! curl -f -s "http://localhost:8080/health" > /dev/null; then
        log_error "Health endpoint check failed"
        return 1
    fi
    
    # Performance validation
    if ! "$PROJECT_ROOT/build/trading_exchange" --validate-performance; then
        log_error "Performance validation failed"
        return 1
    fi
    
    # Security validation
    if ! "$PROJECT_ROOT/build/trading_exchange" --validate-security; then
        log_error "Security validation failed"
        return 1
    fi
    
    log_info "Post-deployment validation passed"
}

cleanup_deployment() {
    log_step "Cleaning up deployment artifacts"
    
    # Remove temporary files
    rm -f /tmp/load_test_config.json
    rm -f /tmp/rtes_last_backup
    
    # Clean up old backups (keep last 5)
    if [[ -d "/var/backups/rtes" ]]; then
        find /var/backups/rtes -maxdepth 1 -type d | sort -r | tail -n +6 | xargs rm -rf 2>/dev/null || true
    fi
    
    log_info "Cleanup completed"
}

send_deployment_notification() {
    local status=$1
    local message="RTES Production Deployment $status: Version $VERSION"
    
    # Send to monitoring systems
    curl -X POST "http://monitoring.internal/api/notifications" \
         -H "Content-Type: application/json" \
         -d "{\"message\": \"$message\", \"severity\": \"info\"}" \
         2>/dev/null || true
    
    # Log structured notification
    "$PROJECT_ROOT/build/trading_exchange" --log-deployment-event "$status" "$VERSION" || true
    
    log_info "Deployment notification sent: $status"
}

main() {
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --deployment-type)
                DEPLOYMENT_TYPE="$2"
                shift 2
                ;;
            --enable-chaos)
                ENABLE_CHAOS_TESTING="true"
                shift
                ;;
            --skip-load-test)
                SKIP_LOAD_TESTING="true"
                shift
                ;;
            --no-rollback)
                ROLLBACK_ON_FAILURE="false"
                shift
                ;;
            --no-backup)
                BACKUP_BEFORE_DEPLOY="false"
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
                if [[ -z "$VERSION" ]]; then
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
    if [[ -z "$VERSION" ]]; then
        log_error "Version is required"
        usage
        exit 1
    fi
    
    # Validate version format
    if [[ ! "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        log_error "Invalid version format: $VERSION (expected: vX.Y.Z)"
        exit 1
    fi
    
    log_info "Starting RTES production deployment: $VERSION"
    log_info "Deployment type: $DEPLOYMENT_TYPE"
    log_info "Chaos testing: $ENABLE_CHAOS_TESTING"
    log_info "Load testing: $([ "$SKIP_LOAD_TESTING" == "false" ] && echo "enabled" || echo "disabled")"
    
    # Set up error handling
    trap 'log_error "Deployment failed"; send_deployment_notification "FAILED"; exit 1' ERR
    
    # Execute deployment pipeline
    validate_prerequisites
    run_production_readiness_checks
    create_backup
    run_chaos_engineering_tests
    run_load_testing
    
    # Execute deployment based on type
    case "$DEPLOYMENT_TYPE" in
        "blue_green")
            deploy_blue_green
            ;;
        "canary")
            deploy_canary
            ;;
        "rolling")
            deploy_rolling
            ;;
        *)
            log_error "Unknown deployment type: $DEPLOYMENT_TYPE"
            exit 1
            ;;
    esac
    
    post_deployment_validation
    cleanup_deployment
    send_deployment_notification "SUCCESS"
    
    log_info "RTES production deployment completed successfully!"
    log_info "Version $VERSION is now live in production"
}

# Run main function
main "$@"