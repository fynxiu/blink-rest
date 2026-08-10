import Foundation

enum BreakStage: CaseIterable, Equatable, Sendable {
    case lookFar
    case blink
    case closeEyes
}

struct BreakSegment: Equatable, Sendable {
    let stage: BreakStage
    let startsAt: TimeInterval
    let duration: TimeInterval

    var endsAt: TimeInterval {
        startsAt + duration
    }
}

struct BreakPhase: Equatable, Sendable {
    let stage: BreakStage
    let elapsed: TimeInterval
    let remaining: TimeInterval
    let progress: Double
}

struct BreakPlan: Equatable, Sendable {
    let totalDuration: TimeInterval
    let segments: [BreakSegment]

    func phase(at elapsed: TimeInterval) -> BreakPhase {
        let clampedElapsed = min(max(0, elapsed), totalDuration)
        let segment = segments.first(where: { clampedElapsed < $0.endsAt }) ?? segments[segments.count - 1]
        let stageElapsed = min(max(0, clampedElapsed - segment.startsAt), segment.duration)

        return BreakPhase(
            stage: segment.stage,
            elapsed: stageElapsed,
            remaining: max(0, segment.duration - stageElapsed),
            progress: segment.duration > 0 ? stageElapsed / segment.duration : 1
        )
    }
}

enum BreakProtocol {
    static func plan(totalDuration: TimeInterval) -> BreakPlan {
        let durations: [TimeInterval]

        switch totalDuration {
        case 10:
            durations = [2, 5, 3]
        case 30:
            durations = [3, 12, 15]
        case 20:
            durations = [3, 9, 8]
        default:
            durations = [3, 9, 8]
        }

        let stages = BreakStage.allCases
        var offset: TimeInterval = 0
        let segments = zip(stages, durations).map { stage, duration in
            defer { offset += duration }
            return BreakSegment(stage: stage, startsAt: offset, duration: duration)
        }

        return BreakPlan(totalDuration: durations.reduce(0, +), segments: segments)
    }
}
