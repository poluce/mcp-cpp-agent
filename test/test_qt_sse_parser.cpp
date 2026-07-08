#include "QtSseParser.h"
#include "tests/common.h"

void test_qt_sse_parser_reads_multiline_data() {
    mcp_qt::QtSseParser parser;
    mcp_qt::QtSseEvent event;
    bool emitted = false;

    parser.setEventCallback([&](const mcp_qt::QtSseEvent& e) {
        event = e;
        emitted = true;
    });

    parser.pushChunk("event: message\ndata: {\"a\":1}\ndata: {\"b\":2}\nid: 55\n\n");

    TM_ASSERT_TRUE(emitted, "Parser should emit complete event");
    TM_ASSERT_EQ(event.eventName, std::string("message"), "Event type should match");
    TM_ASSERT_EQ(event.data, std::string("{\"a\":1}\n{\"b\":2}"), "Data should preserve multi-line join");
    TM_ASSERT_EQ(event.lastEventId, std::string("55"), "Event id should match");
}

void test_qt_sse_parser_reads_retry() {
    mcp_qt::QtSseParser parser;
    int retryMs = 0;

    parser.setRetryCallback([&](int value) { retryMs = value; });
    parser.pushChunk("retry: 1500\n\n");

    TM_ASSERT_EQ(retryMs, 1500, "Retry should be parsed");
}

void test_qt_sse_parser_reads_crlf_events() {
    mcp_qt::QtSseParser parser;
    mcp_qt::QtSseEvent event;
    bool emitted = false;

    parser.setEventCallback([&](const mcp_qt::QtSseEvent& e) {
        event = e;
        emitted = true;
    });

    parser.pushChunk("event: update\r\ndata: {\"crlf\":true}\r\nid: 99\r\n\r\n");

    TM_ASSERT_TRUE(emitted, "Parser should emit complete CRLF event");
    TM_ASSERT_EQ(event.eventName, std::string("update"), "CRLF event type should match");
    TM_ASSERT_EQ(event.data, std::string("{\"crlf\":true}"), "CRLF data should match");
    TM_ASSERT_EQ(event.lastEventId, std::string("99"), "CRLF event id should match");
}
