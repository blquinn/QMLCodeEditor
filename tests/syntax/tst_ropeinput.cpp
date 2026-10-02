#include "syntax/ropeinput.h"

#include <QtTest/QtTest>

using namespace qce;
using namespace Qt::StringLiterals;

namespace {
extern "C" const TSLanguage *tree_sitter_cpp();

QString sexp(TSTree *tree) {
  char *s = ts_node_string(ts_tree_root_node(tree));
  const QString out = QString::fromUtf8(s);
  free(s);
  return out;
}
} // namespace

class TstRopeInput : public QObject {
  Q_OBJECT
private slots:
  // A rope built from many small pieces (so leaves split the text) parses to the same tree as the
  // flat string, including text with surrogate pairs.
  void sameTreeAsFlatString() {
    QString text;
    for (int i = 0; i < 400; ++i)
      text += u"int f%1(int x) { return x + %1; } // \U0001F600 café\n"_s.arg(i);
    RopeBuilder builder;
    for (qsizetype i = 0; i < text.size(); i += 777)
      builder.append(QStringView(text).mid(i, 777));
    const Rope rope = builder.finish();
    QVERIFY(rope.stats().leaves > 1);

    ParserPtr parser(ts_parser_new());
    ts_parser_set_language(parser.get(), tree_sitter_cpp());
    RopeInput input(rope);
    TreePtr viaRope(ts_parser_parse(parser.get(), nullptr, input.input()));
    QVERIFY(viaRope);

    ParserPtr parser2(ts_parser_new());
    ts_parser_set_language(parser2.get(), tree_sitter_cpp());
    TreePtr flat(ts_parser_parse_string_encoding(
      parser2.get(), nullptr, reinterpret_cast<const char *>(text.utf16()), uint32_t(text.size() * 2),
      TSInputEncodingUTF16LE
    ));
    QVERIFY(flat);
    QCOMPARE(sexp(viaRope.get()), sexp(flat.get()));
    QCOMPARE(ts_node_end_byte(ts_tree_root_node(viaRope.get())), uint32_t(text.size() * 2));
  }

  void emptyRope() {
    ParserPtr parser(ts_parser_new());
    ts_parser_set_language(parser.get(), tree_sitter_cpp());
    RopeInput input{Rope()};
    TreePtr tree(ts_parser_parse(parser.get(), nullptr, input.input()));
    QVERIFY(tree);
    QCOMPARE(ts_node_end_byte(ts_tree_root_node(tree.get())), 0u);
  }
};

QTEST_GUILESS_MAIN(TstRopeInput)
#include "tst_ropeinput.moc"
