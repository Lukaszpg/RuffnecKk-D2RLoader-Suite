#pragma once

#include "navigation_engine.hpp"

#include <cstdint>

namespace RuffnecKk::MapSense {

enum class GpsRouteReadiness : std::uint8_t {
    Ready,
    SourceInactive,
    SourceContended,
    SourceUnknownLevel,
    NoDestinations,
    NoPlayer,
    SourceException,
    NoGeometry,
    NoGeometryPayload,
    NoCatalog,
    PolicyMismatch,
    SessionMismatch,
    LevelMismatch,
    GeometrySessionMismatch,
    InvalidDifficulty,
    InvalidDigest,
};

struct GpsRouteReadinessInput final {
    NavigationGpsSourceReadiness sourceReadiness{
        NavigationGpsSourceReadiness::Inactive};
    bool hasGeometry{};
    bool hasGeometryPayload{};
    bool hasCatalog{};
    std::uint64_t sourceSession{};
    std::uint64_t currentSession{};
    std::uint64_t sourcePolicy{};
    std::uint64_t currentPolicy{};
    std::uint64_t geometrySession{};
    std::uint64_t geometryDigest{};
    std::int32_t sourceLevel{};
    std::int32_t geometryLevel{};
    std::uint8_t geometryDifficulty{};
};

[[nodiscard]] constexpr auto EvaluateGpsRouteReadiness(
        const GpsRouteReadinessInput& input) noexcept -> GpsRouteReadiness {
    switch (input.sourceReadiness) {
        case NavigationGpsSourceReadiness::Ready:
            break;
        case NavigationGpsSourceReadiness::Inactive:
            return GpsRouteReadiness::SourceInactive;
        case NavigationGpsSourceReadiness::Contended:
            return GpsRouteReadiness::SourceContended;
        case NavigationGpsSourceReadiness::UnknownLevel:
            return GpsRouteReadiness::SourceUnknownLevel;
        case NavigationGpsSourceReadiness::NoDestinations:
            return GpsRouteReadiness::NoDestinations;
        case NavigationGpsSourceReadiness::NoPlayer:
            return GpsRouteReadiness::NoPlayer;
        case NavigationGpsSourceReadiness::Exception:
            return GpsRouteReadiness::SourceException;
    }
    if (!input.hasGeometry) return GpsRouteReadiness::NoGeometry;
    if (!input.hasGeometryPayload) return GpsRouteReadiness::NoGeometryPayload;
    if (!input.hasCatalog) return GpsRouteReadiness::NoCatalog;
    if (input.sourcePolicy != input.currentPolicy) {
        return GpsRouteReadiness::PolicyMismatch;
    }
    if (input.sourceSession == 0U
        || input.sourceSession != input.currentSession) {
        return GpsRouteReadiness::SessionMismatch;
    }
    if (input.sourceLevel <= 0 || input.sourceLevel != input.geometryLevel) {
        return GpsRouteReadiness::LevelMismatch;
    }
    if (input.geometrySession != input.sourceSession) {
        return GpsRouteReadiness::GeometrySessionMismatch;
    }
    if (input.geometryDifficulty > 2U) {
        return GpsRouteReadiness::InvalidDifficulty;
    }
    if (input.geometryDigest == 0U) return GpsRouteReadiness::InvalidDigest;
    return GpsRouteReadiness::Ready;
}

[[nodiscard]] constexpr auto GpsRouteReadinessName(
        GpsRouteReadiness readiness) noexcept -> const char* {
    switch (readiness) {
        case GpsRouteReadiness::Ready: return "ready";
        case GpsRouteReadiness::SourceInactive: return "source-inactive";
        case GpsRouteReadiness::SourceContended: return "source-contended";
        case GpsRouteReadiness::SourceUnknownLevel:
            return "source-unknown-level";
        case GpsRouteReadiness::NoDestinations: return "no-destinations";
        case GpsRouteReadiness::NoPlayer: return "no-player";
        case GpsRouteReadiness::SourceException: return "source-exception";
        case GpsRouteReadiness::NoGeometry: return "no-geometry";
        case GpsRouteReadiness::NoGeometryPayload:
            return "no-geometry-payload";
        case GpsRouteReadiness::NoCatalog: return "no-catalog";
        case GpsRouteReadiness::PolicyMismatch: return "policy-mismatch";
        case GpsRouteReadiness::SessionMismatch: return "session-mismatch";
        case GpsRouteReadiness::LevelMismatch: return "level-mismatch";
        case GpsRouteReadiness::GeometrySessionMismatch:
            return "geometry-session-mismatch";
        case GpsRouteReadiness::InvalidDifficulty:
            return "invalid-difficulty";
        case GpsRouteReadiness::InvalidDigest: return "invalid-digest";
    }
    return "unknown";
}

} // namespace RuffnecKk::MapSense
