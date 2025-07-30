/*
 * Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
 * This product includes software developed at Datadog (https://www.datadoghq.com/).
 * Copyright 2019-Present Datadog, Inc.
 */

import XCTest
import TestUtilities
import DatadogInternal
@testable import DatadogRUM

final class RUMViewEventsFilterTests: XCTestCase {
    let sut = RUMViewEventsFilter()

    // MARK: - Base cases

    func testFilterWhenNoEvents() throws {
        let events = [Event]()

        let actual = sut.filter(events: events)
        let expected = [Event]()

        XCTAssertEqual(actual, expected)
    }

    func testFilterWhenNoMetadata() throws {
        let events = [
            try Event(data: "A.1", metadata: nil),
            try Event(data: "A.2", metadata: nil),
            try Event(data: "A.3", metadata: nil),
            try Event(data: "A.4", metadata: nil)
        ]

        let actual = sut.filter(events: events)

        XCTAssertEqual(actual, events)
    }

    func testFilterWhenMixedMissingMetadata() throws {
        let events = [
            try Event(data: "A.1", metadata: nil),
            try Event(data: "A.2", metadata: nil),
            try Event(data: "B.1", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 1)),
            try Event(data: "B.2", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 2)),
            try Event(data: "C.1", metadata: nil),
            try Event(data: "B.3", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 3)),
            try Event(data: "A.3", metadata: nil)
        ]

        let actual = sut.filter(events: events)
        let expected = [
            try Event(data: "A.1", metadata: nil),
            try Event(data: "A.2", metadata: nil),
            try Event(data: "C.1", metadata: nil),
            try Event(data: "B.3", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 3)),
            try Event(data: "A.3", metadata: nil)
        ]

        XCTAssertEqual(actual, expected)
    }

    // MARK: - Common filtering scenarios

    func testFilterWhenSameEvent() throws {
         let events = [
            try Event(data: "A.1", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 1)),
            try Event(data: "A.2", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 2)),
            try Event(data: "A.3", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 3)),
            try Event(data: "A.4", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 4))
         ]

        let actual = sut.filter(events: events)
        let expected = [
            try Event(data: "A.4", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 4))
        ]

        XCTAssertEqual(actual, expected)
    }

    func testFilterWhenMixedEvents() throws {
          let events = [
            try Event(data: "B.1", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 1)),
            try Event(data: "A.5", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 5)),
            try Event(data: "B.2", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 2)),
          ]

        let actual = sut.filter(events: events)
        let expected = [
            try Event(data: "A.5", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 5)),
            try Event(data: "B.2", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 2)),
        ]

        XCTAssertEqual(actual, expected)
    }

    func testFilterWhenSingleEvent() throws {
        let events = [
            try Event(data: "B.3", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 3)),
        ]

        let actual = sut.filter(events: events)
        let expected = [
            try Event(data: "B.3", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 3)),
        ]

        XCTAssertEqual(actual, expected)
    }

    func testFilterAlwaysKeepsEventsWithAccessibility() throws {
        // Test that events with accessibility are always kept, even if they would normally be filtered out
        let events = [
            try Event(data: "A.1", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 1, hasAccessibility: false)),
            try Event(data: "A.2", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 2, hasAccessibility: true)), // This should be kept
            try Event(data: "A.3", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 3, hasAccessibility: false)),
            try Event(data: "A.4", metadata: RUMViewEvent.Metadata(id: "A", documentVersion: 4, hasAccessibility: false)),
            try Event(data: "B.1", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 1, hasAccessibility: false)),
            try Event(data: "B.2", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 2, hasAccessibility: true)), // This should be kept
            try Event(data: "B.3", metadata: RUMViewEvent.Metadata(id: "B", documentVersion: 3, hasAccessibility: false)),
        ]

        let actual = sut.filter(events: events)

        // Verify that accessibility events are kept
        let accessibilityEvents = actual.filter { event in
            guard let metadata = event.metadata,
                  let viewMetadata = try? JSONDecoder().decode(RUMViewEvent.Metadata.self, from: metadata) else {
                return false
            }
            return viewMetadata.hasAccessibility == true
        }

        // Should have 2 accessibility events (A.2 and B.2)
        XCTAssertEqual(accessibilityEvents.count, 2, "Accessibility events should always be kept")

        // Verify the specific accessibility events are present
        let hasA2 = actual.contains { event in
            guard let metadata = event.metadata,
                  let viewMetadata = try? JSONDecoder().decode(RUMViewEvent.Metadata.self, from: metadata) else {
                return false
            }
            return viewMetadata.id == "A" && viewMetadata.documentVersion == 2 && viewMetadata.hasAccessibility
        }

        let hasB2 = actual.contains { event in
            guard let metadata = event.metadata,
                  let viewMetadata = try? JSONDecoder().decode(RUMViewEvent.Metadata.self, from: metadata) else {
                return false
            }
            return viewMetadata.id == "B" && viewMetadata.documentVersion == 2 && viewMetadata.hasAccessibility
        }

        XCTAssertTrue(hasA2, "Event A.2 with accessibility should be kept")
        XCTAssertTrue(hasB2, "Event B.2 with accessibility should be kept")
    }
}

extension Event {
    init(data: String, metadata: RUMViewEvent.Metadata?) throws {
        let encoder = JSONEncoder()
        encoder.outputFormatting = .sortedKeys
        self.init(data: data.utf8Data, metadata: try encoder.encode(metadata))
    }
}
