import XCTest
@testable import BlinkRest

final class BreakProtocolTests: XCTestCase {
    func testSupportedPlansHaveExpectedStageDurations() {
        assertPlan(total: 10, durations: [2, 5, 3])
        assertPlan(total: 20, durations: [3, 9, 8])
        assertPlan(total: 30, durations: [3, 12, 15])
        assertPlan(total: 45, durations: [4.5, 18, 22.5])
        assertPlan(total: 60, durations: [6, 24, 30])
        assertPlan(total: 90, durations: [9, 36, 45])
    }

    func testUnsupportedDurationFallsBackToThirtySecondPlan() {
        let fallback = BreakProtocol.plan(totalDuration: 7)
        let standard = BreakProtocol.plan(totalDuration: 30)

        XCTAssertEqual(fallback, standard)
        XCTAssertEqual(BreakProtocol.plan(totalDuration: .nan), standard)
    }

    func testTenSecondPlanUsesHalfOpenStageBoundaries() {
        let plan = BreakProtocol.plan(totalDuration: 10)

        XCTAssertEqual(plan.phase(at: -1).stage, .lookFar)
        XCTAssertEqual(plan.phase(at: 1.999).stage, .lookFar)
        XCTAssertEqual(plan.phase(at: 2).stage, .blink)
        XCTAssertEqual(plan.phase(at: 6.999).stage, .blink)
        XCTAssertEqual(plan.phase(at: 7).stage, .closeEyes)
        XCTAssertEqual(plan.phase(at: 10).stage, .closeEyes)
        XCTAssertEqual(plan.phase(at: 99).stage, .closeEyes)
    }

    func testPhaseProgressIsRelativeToCurrentStage() {
        let phase = BreakProtocol.plan(totalDuration: 20).phase(at: 7.5)

        XCTAssertEqual(phase.stage, .blink)
        XCTAssertEqual(phase.elapsed, 4.5, accuracy: 0.0001)
        XCTAssertEqual(phase.remaining, 4.5, accuracy: 0.0001)
        XCTAssertEqual(phase.progress, 0.5, accuracy: 0.0001)
    }

    func testBreakSessionClampsElapsedRemainingAndProgress() {
        let session = BreakSession(
            startedAt: MonotonicInstant(seconds: 100),
            endsAt: MonotonicInstant(seconds: 110),
            durationSnapshot: 10
        )

        XCTAssertEqual(session.elapsed(at: MonotonicInstant(seconds: 90)), 0)
        XCTAssertEqual(session.remaining(at: MonotonicInstant(seconds: 90)), 10)
        XCTAssertEqual(session.progress(at: MonotonicInstant(seconds: 105)), 0.5)
        XCTAssertEqual(session.elapsed(at: MonotonicInstant(seconds: 120)), 10)
        XCTAssertEqual(session.remaining(at: MonotonicInstant(seconds: 120)), 0)
        XCTAssertEqual(session.progress(at: MonotonicInstant(seconds: 120)), 1)
    }

    private func assertPlan(total: TimeInterval, durations: [TimeInterval]) {
        let plan = BreakProtocol.plan(totalDuration: total)

        XCTAssertEqual(plan.totalDuration, total)
        XCTAssertEqual(plan.segments.map(\.stage), [.lookFar, .blink, .closeEyes])
        XCTAssertEqual(plan.segments.map(\.duration), durations)
        XCTAssertEqual(plan.segments.map(\.startsAt), [0, durations[0], durations[0] + durations[1]])
    }
}
