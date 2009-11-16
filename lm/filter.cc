/* Filter an ARPA language model to only contain words found in a vocabulary
 * plus <s>, </s>, and <unk>.
 */

#include "lm/filter.hh"

#include "util/multi_intersection.hh"
#include "util/string_piece.hh"
#include "util/tokenize_piece.hh"

#include <boost/lexical_cast.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/unordered/unordered_map.hpp>
#include <boost/unordered/unordered_set.hpp>

#include <istream>
#include <ostream>
#include <memory>
#include <string>
#include <vector>

#include <err.h>
#include <string.h>

namespace lm {

// Seeking is the responsibility of the caller.
void WriteCounts(std::ostream &out, const std::vector<size_t> &number) {
  out << "\n\\data\\\n";
  for (unsigned int i = 0; i < number.size(); ++i) {
    out << "ngram " << i+1 << "=" << number[i] << '\n';
  }
  out << '\n';
}

size_t SizeNeededForCounts(const std::vector<size_t> &number) {
  std::ostringstream buf;
  WriteCounts(buf, number);
  return buf.tellp();
}

void ReadCounts(std::istream &in, std::vector<size_t> &number) {
  number.clear();
  std::string line;
  if (!getline(in, line)) err(2, "Reading input lm");
  if (!line.empty()) errx(3, "First line was \"%s\", not blank.", line.c_str());
  if (!getline(in, line)) err(2, "Reading \\data\\");
  if (!(line == "\\data\\")) err(3, "Second line was \"%s\", not blank.", line.c_str());
  while (getline(in, line)) {
    if (line.empty()) return;
    if (strncmp(line.c_str(), "ngram ", 6))
      errx(3, "data line \"%s\" doesn't begin with \"ngram \"", line.c_str());
    size_t equals = line.find('=');
    if (equals == std::string::npos)
      errx(3, "no equals in \"%s\".", line.c_str());
    unsigned int length = boost::lexical_cast<unsigned int>(line.substr(6, equals - 6));
    if (length - 1 != number.size()) errx(3, "ngram length %i is not expected %i in line %s", length, static_cast<unsigned int>(number.size() + 1), line.c_str());
    unsigned int count = boost::lexical_cast<unsigned int>(line.substr(equals + 1));
    number.push_back(count);		
  }
  err(2, "Reading input lm");
}

void ReadNGramHeader(std::istream &in, unsigned int length) {
  std::string line;
  do {
    if (!getline(in, line)) err(2, "Reading from input lm");
  } while (line.empty());
  if (line != (std::string("\\") + boost::lexical_cast<std::string>(length) + "-grams:"))
    errx(3, "Wrong ngram line: %s", line.c_str());
}

void ReadEnd(std::istream &in_lm) {
  std::string line;
  if (!getline(in_lm, line)) err(2, "Reading from input lm");
  if (line != "\\end\\") errx(3, "Bad end \"%s\"", line.c_str());
}

OutputLM::OutputLM(const char *name)  {
  file_.exceptions(std::ostream::eofbit | std::ostream::failbit | std::ostream::badbit);
  file_.open(name, std::ios::out);
}

void OutputLM::ReserveForCounts(std::streampos reserve) {
  for (std::streampos i = 0; i < reserve; i += std::streampos(1)) {
    file_ << '\n';
  }
}

void OutputLM::BeginLength(unsigned int length) {
  fast_counter_ = 0;
  file_ << '\\' << length << "-grams:" << '\n';
}

void OutputLM::EndLength(unsigned int length) {
  file_ << '\n';
  if (length > counts_.size()) {
    counts_.resize(length);
  }
  counts_[length - 1] = fast_counter_;
}

void OutputLM::Finish() {
  file_ << "\\end\\\n";

  file_.seekp(0);
  WriteCounts(file_, counts_);
  file_ << std::flush;
}

SingleVocabFilter::SingleVocabFilter(std::istream &vocab, const char *out) : SingleOutputFilter(out) {
  std::auto_ptr<std::string> word(new std::string());
  while (vocab >> *word) {
    if (words_.insert(StringPiece(*word)).second) {
      backing_.push_back(word);
      word.reset(new std::string());
    }
  }
  if (!vocab.eof()) err(1, "Reading text from stdin");
}

MultipleVocabMultipleOutputFilter::MultipleVocabMultipleOutputFilter(const Map &vocabs, unsigned int sentence_count, const char *prefix) : vocabs_(vocabs) {
  files_.reserve(sentence_count);
  std::string tmp;
  for (unsigned int i = 0; i < sentence_count; ++i) {
    tmp = prefix;
    tmp += boost::lexical_cast<std::string>(i);
    files_.push_back(new OutputLM(tmp.c_str()));
  }
}

} // namespace lm