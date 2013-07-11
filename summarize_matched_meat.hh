#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <iterator>
#include <vector>
#include <list>
#include <omp.h>
#include <boost/program_options.hpp>
#include <boost/foreach.hpp>
#define N_POLICY 2
#include "blast.hh"
#include "psl.hh"
#include "pairset.hh"
#include "kpairset.hh"
#include "kmerset.hh"
#include "mask.hh"
#include "util.hh"

struct stats_tuple
{
  double pair, kpair, kmer, nucl, tran;
};

double compute_F1(double precis, double recall)
{
  if (precis == 0.0 && recall == 0.0)
    return 0.0;
  else
    return 2*precis*recall/(precis+recall);
}

void print_stats(const stats_tuple& precis, const stats_tuple& recall, const std::string& prefix)
{
  std::cout << prefix << "_pair_precision\t" << precis.pair                          << std::endl;
  std::cout << prefix << "_pair_recall\t"    << recall.pair                          << std::endl;
  std::cout << prefix << "_pair_F1\t"        << compute_F1(precis.pair, recall.pair) << std::endl;

  std::cout << prefix << "_kpair_precision\t" << precis.kpair                          << std::endl;
  std::cout << prefix << "_kpair_recall\t"    << recall.kpair                          << std::endl;
  std::cout << prefix << "_kpair_F1\t"        << compute_F1(precis.kpair, recall.kpair) << std::endl;

  std::cout << prefix << "_kmer_precision\t" << precis.kmer                          << std::endl;
  std::cout << prefix << "_kmer_recall\t"    << recall.kmer                          << std::endl;
  std::cout << prefix << "_kmer_F1\t"        << compute_F1(precis.kmer, recall.kmer) << std::endl;

  std::cout << prefix << "_nucl_precision\t" << precis.nucl                          << std::endl;
  std::cout << prefix << "_nucl_recall\t"    << recall.nucl                          << std::endl;
  std::cout << prefix << "_nucl_F1\t"        << compute_F1(precis.nucl, recall.nucl) << std::endl;

  std::cout << prefix << "_tran_precision\t" << precis.tran                          << std::endl;
  std::cout << prefix << "_tran_recall\t"    << recall.tran                          << std::endl;
  std::cout << prefix << "_tran_F1\t"        << compute_F1(precis.tran, recall.tran) << std::endl;
}

struct tagged_alignment
{
  size_t a_idx, b_idx;
  std::vector<alignment_segment> segments;
  double contribution;
};

struct compare_tagged_alignments
{
  bool operator()(const tagged_alignment *l1, const tagged_alignment *l2) const
  {
    return l1->contribution < l2->contribution;
  }
};

struct pair_helper
{
  const std::vector<size_t>& B_lengths;
  const std::vector<double>& tau_B;
  double                     numer;

  pair_helper(const std::vector<size_t>& B_lengths,
              const std::vector<double>& tau_B)
  : B_lengths(B_lengths), tau_B(tau_B), numer(0.0)
  {}

  double compute_contribution(const tagged_alignment& l) const
  {
    smart_pairset b_pairset(B_lengths[l.b_idx]);
    BOOST_FOREACH(const alignment_segment& seg, l.segments)
      b_pairset.add_square_with_exceptions(seg.b_start, seg.b_end, seg.b_mismatches.begin(), seg.b_mismatches.end());
    return tau_B[l.b_idx] * b_pairset.size();
  }

  void add_contribution_to_recall(const tagged_alignment& l) { numer += l.contribution; }

  double get_recall()
  {
    double denom = 0.0;
    for (size_t b_idx = 0; b_idx < B_lengths.size(); ++b_idx)
      denom += tau_B[b_idx] * B_lengths[b_idx] * (B_lengths[b_idx] + 1) / 2;
    return numer/denom;
  }
};

struct kpair_helper
{
  size_t                     k;
  const std::vector<size_t>& B_lengths;
  const std::vector<double>& tau_B;
  double                     numer;

  kpair_helper(size_t k,
               const std::vector<size_t>& B_lengths,
               const std::vector<double>& tau_B)
  : k(k), B_lengths(B_lengths), tau_B(tau_B), numer(0.0)
  {}

  double compute_contribution(const tagged_alignment& l) const
  {
    kpairset b_kpairset(k, B_lengths[l.b_idx]);
    BOOST_FOREACH(const alignment_segment& seg, l.segments)
      b_kpairset.add_kpairs_with_exceptions(seg.b_start, seg.b_end, seg.b_mismatches.begin(), seg.b_mismatches.end());
    return tau_B[l.b_idx] * b_kpairset.size();
  }

  void add_contribution_to_recall(const tagged_alignment& l) { numer += l.contribution; }

  double get_recall()
  {
    // How many k-pairs are there in a seq of length n?
    // There are n-k+2 such k-pairs.
    // 
    // E.g., k=3, n=5
    // 01234  -> (0,2), (1,3), (2,4)
    // Indeed, 5-3+1 = 3
    double denom = 0.0;
    for (size_t b_idx = 0; b_idx < B_lengths.size(); ++b_idx) {
      if (B_lengths[b_idx] >= k)
        denom += tau_B[b_idx] * (B_lengths[b_idx] - k + 1);
    }
    return numer/denom;
  }
};

struct kmer_helper
{
  size_t                     k;
  const std::vector<size_t>& B_lengths;
  const std::vector<double>& tau_B;
  double                     numer;

  kmer_helper(size_t k,
              const std::vector<size_t>& B_lengths,
              const std::vector<double>& tau_B)
  : k(k), B_lengths(B_lengths), tau_B(tau_B), numer(0.0)
  {}

  double compute_contribution(const tagged_alignment& l) const
  {
    kmerset b_kmerset(k, B_lengths[l.b_idx]);
    BOOST_FOREACH(const alignment_segment& seg, l.segments)
      b_kmerset.add_kmers_with_exceptions(seg.b_start, seg.b_end, seg.b_mismatches.begin(), seg.b_mismatches.end());
    return tau_B[l.b_idx] * b_kmerset.size();
  }

  void add_contribution_to_recall(const tagged_alignment& l) { numer += l.contribution; }

  double get_recall()
  {
    // How many kmers are there in a seq of length n?
    // There are n-k+2 such kmers.
    // 
    // E.g., k=3, n=5
    // 01234  -> (0,1,2), (1,2,3), (2,3,4)
    // Indeed, 5-3+1 = 3
    double denom = 0.0;
    for (size_t b_idx = 0; b_idx < B_lengths.size(); ++b_idx) {
      if (B_lengths[b_idx] >= k)
        denom += tau_B[b_idx] * (B_lengths[b_idx] - k + 1);
    }
    return numer/denom;
  }
};

struct nucl_helper
{
  const std::vector<size_t>& B_lengths;
  const std::vector<double>& tau_B;
  double                     numer;

  nucl_helper(const std::vector<size_t>& B_lengths,
              const std::vector<double>& tau_B)
  : B_lengths(B_lengths), tau_B(tau_B), numer(0.0)
  {}

  double compute_contribution(const tagged_alignment& l) const
  {
    mask b_mask(B_lengths[l.b_idx]);
    BOOST_FOREACH(const alignment_segment& seg, l.segments)
      b_mask.add_interval_with_exceptions(seg.b_start, seg.b_end, seg.b_mismatches.begin(), seg.b_mismatches.end());
    return tau_B[l.b_idx] * b_mask.num_ones();
  }

  void add_contribution_to_recall(const tagged_alignment& l) { numer += l.contribution; }

  double get_recall()
  {
    double denom = 0.0;
    for (size_t b_idx = 0; b_idx < B_lengths.size(); ++b_idx)
      denom += tau_B[b_idx] * B_lengths[b_idx];
    return numer/denom;
  }
};

struct tran_helper
{
  const std::vector<size_t>& B_lengths;
  const std::vector<double>& tau_B;
  std::vector<mask>          B_mask;
  std::vector<double>        B_frac_ones;

  tran_helper(const std::vector<size_t>& B_lengths,
              const std::vector<double>& tau_B)
  : B_lengths(B_lengths), tau_B(tau_B), B_frac_ones(B_lengths.size())
  {
    BOOST_FOREACH(size_t b_length, B_lengths)
      B_mask.push_back(mask(b_length));
  }

  double compute_contribution(const tagged_alignment& l) const
  {
    mask b_mask(B_lengths[l.b_idx]);
    BOOST_FOREACH(const alignment_segment& seg, l.segments)
      b_mask.add_interval_with_exceptions(seg.b_start, seg.b_end, seg.b_mismatches.begin(), seg.b_mismatches.end());
    return tau_B[l.b_idx] * b_mask.num_ones();
  }

  void add_contribution_to_recall(const tagged_alignment& l)
  {
    mask& m = B_mask[l.b_idx];
    BOOST_FOREACH(const alignment_segment& seg, l.segments)
      m.add_interval_with_exceptions(seg.b_start, seg.b_end, seg.b_mismatches.begin(), seg.b_mismatches.end());
  }

  double get_recall()
  {
    double recall = 0.0;
    for (size_t b_idx = 0; b_idx < B_lengths.size(); ++b_idx) {
      B_frac_ones[b_idx] = (1.0 * B_mask[b_idx].num_ones()) / B_lengths[b_idx];
      if (B_frac_ones[b_idx] >= 0.95)
        recall += tau_B[b_idx];
    }
    return recall;
  }
};

inline bool is_good_enough_for_do_it_all(const std::vector<alignment_segment>& segs, size_t min_seg_len)
{
  size_t len = 0;
  BOOST_FOREACH(const alignment_segment& seg, segs) {
    if (seg.a_end >= seg.a_start)
      len += seg.a_end - seg.a_start + 1;
    else
      len += seg.a_start - seg.a_end + 1;
  }
  return len >= min_seg_len;
}

inline bool is_valid(const psl_alignment& al, bool strand_specific)
{
  if (!strand_specific)
    return true;
  else
    return al.strand() == "+";
}

inline bool is_valid(const blast_alignment& /*al*/, bool strand_specific)
{
  if (!strand_specific)
    return true;
  else
    throw std::runtime_error("Strand-specificity has not been implemented yet for blast alignments.");
}

// Reads alignments, perfoms initial filtering, and converts the alignments to
// segments. However, does *not* compute the initial contributions.
template<typename AlignmentType>
void read_alignments(std::vector<tagged_alignment>& alignments,
                     typename AlignmentType::input_stream_type&     input_stream,
                     const std::vector<std::string>&                A,
                     const std::vector<std::string>&                B,
                     const std::map<std::string, size_t>&           A_names_to_idxs,
                     const std::map<std::string, size_t>&           B_names_to_idxs,
                     bool                                           strand_specific,
                     size_t                                         readlen)
{
  AlignmentType al;
  tagged_alignment l;
  while (input_stream >> al) {
    if (is_valid(al, strand_specific)) {
      l.a_idx = A_names_to_idxs.find(al.a_name())->second;
      l.b_idx = B_names_to_idxs.find(al.b_name())->second;
      typename AlignmentType::segments_type segs = al.segments(A[l.a_idx], B[l.b_idx]);
      l.segments.assign(segs.begin(), segs.end());
      if (is_good_enough_for_do_it_all(l.segments, readlen))
        alignments.push_back(l);
    }
  }
}

// Preconditions:
// - best_from_A should be of size 0
template<typename HelperType>
void do_it_all(HelperType&                   helper,
               std::vector<tagged_alignment> alignments, /* passed by value */
               size_t                        A_card,
               size_t                        B_card,
               size_t                        readlen)
{
  // Compute contributions.
  #pragma omp parallel for
  for (int i = 0; i < static_cast<int>(alignments.size()); ++i) {
    tagged_alignment& l = alignments[i];
    l.contribution = helper.compute_contribution(l);
  }

  // Init vector of pointers to popped alignments.
  std::vector<std::vector<tagged_alignment *> > popped_by_A(A_card);
  std::vector<std::vector<tagged_alignment *> > popped_by_B(B_card);

  // Make priority queue initially filled with all alignments.
  compare_tagged_alignments comparator;
  std::priority_queue<
    tagged_alignment *,
    std::vector<tagged_alignment *>,
    compare_tagged_alignments> Q(comparator);
  BOOST_FOREACH(tagged_alignment& l1, alignments)
    Q.push(&l1);

  while (!Q.empty()) {

    // Pop l1 from Q.
    tagged_alignment *l1 = Q.top();
    Q.pop();

    // Subtract all previous alignments from l1.
    bool l1_has_changed = false;
    typename std::vector<tagged_alignment *>::const_iterator
        l2_a = popped_by_A[l1->a_idx].begin(), end_a = popped_by_A[l1->a_idx].end(),
        l2_b = popped_by_B[l1->b_idx].begin(), end_b = popped_by_B[l1->b_idx].end();
    while (l2_a != end_a || l2_b != end_b) {

      bool l2_b_was_popped_first;
      if (l2_a != end_a && l2_b != end_b) {
        // If *l2_a < *l2_b, i.e., the current alignment in popped_by_A has
        // lower priority than the current alignment in popped_by_B, then *l2_b
        // was popped before *l2_a.
        l2_b_was_popped_first = comparator(*l2_a, *l2_b);
      } else {
        // Since we are still in the for loop (so one of the iterators is not
        // at the end), and we got to this else (so one of the iterators is at
        // the end), we just have to figure out which iterator is still not at
        // the end.
        l2_b_was_popped_first = (l2_b != end_b);
      }

      if (l2_b_was_popped_first) {
        if (intersects<segment_ops_wrt_b>(l1->segments, (*l2_b)->segments)) {
          subtract_in_place<segment_ops_wrt_b>(l1->segments, (*l2_b)->segments);
          l1_has_changed = true;
        }
        ++l2_b;
      } else {
        if (intersects<segment_ops_wrt_a>(l1->segments, (*l2_a)->segments)) {
          subtract_in_place<segment_ops_wrt_a>(l1->segments, (*l2_a)->segments);
          l1_has_changed = true;
        }
        ++l2_a;
      }

    }

    // If the alignment has changed, then put it back in the priority queue.
    if (l1_has_changed) {

      if (is_good_enough_for_do_it_all(l1->segments, readlen)) {
        l1->contribution = helper.compute_contribution(*l1);
        Q.push(l1);
      }

    // Otherwise, if the alignment has not changed, we actually process it.
    } else {

      // Record l1's contribution.
      helper.add_contribution_to_recall(*l1);

      // Add l1 to list of popped alignments.
      popped_by_A[l1->a_idx].push_back(l1);
      popped_by_B[l1->b_idx].push_back(l1);

    }

  }
}

stats_tuple do_it_all_wrapper(const std::vector<tagged_alignment>& alignments,
                              size_t                               A_card,
                              size_t                               B_card,
                              const std::vector<size_t>&           B_lengths,
                              const std::vector<double>&           tau_B,
                              size_t                               readlen,
                              std::vector<double>                  *plot_output) // if non-null, B_frac_ones will be copied here
{
  stats_tuple st;

  {
    pair_helper h(B_lengths, tau_B);
    do_it_all(h, alignments, A_card, B_card, readlen);
    st.pair = h.get_recall();
  }

  {
    kpair_helper h(readlen, B_lengths, tau_B);
    do_it_all(h, alignments, A_card, B_card, readlen);
    st.kpair = h.get_recall();
  }

  {
    kmer_helper h(readlen, B_lengths, tau_B);
    do_it_all(h, alignments, A_card, B_card, readlen);
    st.kmer = h.get_recall();
  }

  {
    nucl_helper h(B_lengths, tau_B);
    do_it_all(h, alignments, A_card, B_card, readlen);
    st.nucl = h.get_recall();
  }

  {
    tran_helper h(B_lengths, tau_B);
    do_it_all(h, alignments, A_card, B_card, readlen);
    st.tran = h.get_recall();
    if (plot_output)
      *plot_output = h.B_frac_ones;
  }

  return st;
}

void parse_options(boost::program_options::variables_map& vm, int argc, const char **argv)
{
  namespace po = boost::program_options;

  po::options_description desc("Options");
  desc.add_options()
    ("help,?", "Display this information.")
    ("A-seqs", po::value<std::string>()->required(), "The assembly sequences, in FASTA format.")
    ("B-seqs", po::value<std::string>()->required(), "The oracleset sequences, in FASTA format.")
    ("A-expr", po::value<std::string>(),             "The assembly expression, as produced by RSEM in a file called *.isoforms.results.")
    ("B-expr", po::value<std::string>(),             "The oracleset expression, as produced by RSEM in a file called *.isoforms.results.")
    ("no-expr",                                      "Do not use expression at all. No weighted scores will be produced.")
    ("A-to-B", po::value<std::string>()->required(), "The alignments of A to B.")
    ("B-to-A", po::value<std::string>()->required(), "The alignments of B to A.")
    ("alignment-type", po::value<std::string>()->required(), "The type of alignments used, either 'blast' or 'psl'.")
    ("plot-output", po::value<std::string>()->required(),   "File where plot values will be written.")
    ("strand-specific",                              "Ignore alignments that are to the reverse strand.")
    ("readlen", po::value<size_t>()->required(),     "The read length.")
  ;

  try {

    po::store(po::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help")) {
      std::cerr << desc << std::endl;
      exit(1);
    }

    po::notify(vm);

    if (vm.count("no-expr")) {
      if (vm.count("A-expr") != 0 || vm.count("B-expr") != 0)
        throw po::error("If --no-expr is given, then --A-expr and --B-expr cannot be given.");
    } else {
      if (vm.count("A-expr") == 0 || vm.count("B-expr") == 0)
        throw po::error("If --no-expr is not given, then --A-expr and --B-expr must be given.");
    }

    if (vm["alignment-type"].as<std::string>() != "blast" &&
        vm["alignment-type"].as<std::string>() != "psl")
      throw po::error("Option --alignment-type needs to be 'blast' or 'psl'.");

  } catch (std::exception& x) {
    std::cerr << "Error: " << x.what() << std::endl;
    std::cerr << desc << std::endl;
    exit(1);
  }
}

template<typename AlignmentType>
void main_1(const boost::program_options::variables_map& vm)
{
  bool strand_specific = vm.count("strand-specific");
  size_t readlen = vm["readlen"].as<size_t>();

  std::cerr << "Reading the sequences" << std::endl;
  std::vector<std::string> A, B;
  std::vector<std::string> A_names, B_names;
  std::map<std::string, size_t> A_names_to_idxs, B_names_to_idxs;
  read_fasta_names_and_seqs(vm["A-seqs"].as<std::string>(), A, A_names, A_names_to_idxs);
  read_fasta_names_and_seqs(vm["B-seqs"].as<std::string>(), B, B_names, B_names_to_idxs);

  std::cerr << "Computing sequence statistics" << std::endl;
  size_t A_card = A.size(), B_card = B.size();
  std::vector<size_t> A_lengths, B_lengths;
  BOOST_FOREACH(const std::string& a, A) A_lengths.push_back(a.size());
  BOOST_FOREACH(const std::string& b, B) B_lengths.push_back(b.size());

  std::vector<double> real_tau_A(A_card), real_tau_B(B_card);
  if (!vm.count("no-expr")) {
    std::cerr << "Reading transcript-level expression for A and B" << std::endl;
    std::string A_expr_fname = vm["A-expr"].as<std::string>();
    std::string B_expr_fname = vm["B-expr"].as<std::string>();
    read_transcript_expression(A_expr_fname, real_tau_A, A_names_to_idxs);
    read_transcript_expression(B_expr_fname, real_tau_B, B_names_to_idxs);
  }

  std::cerr << "Computing uniform transcript-level expression" << std::endl;
  std::vector<double> unif_tau_A(A_card, 1.0/A_card);
  std::vector<double> unif_tau_B(B_card, 1.0/B_card);

  std::cerr << "Reading the alignments" << std::endl;
  std::vector<tagged_alignment> A_to_B, B_to_A;
  typename AlignmentType::input_stream_type A_to_B_is(open_or_throw(vm["A-to-B"].as<std::string>()));
  typename AlignmentType::input_stream_type B_to_A_is(open_or_throw(vm["B-to-A"].as<std::string>()));
  read_alignments<AlignmentType>(A_to_B, A_to_B_is, A, B, A_names_to_idxs, B_names_to_idxs, strand_specific, readlen);
  read_alignments<AlignmentType>(B_to_A, B_to_A_is, B, A, B_names_to_idxs, A_names_to_idxs, strand_specific, readlen);

  std::cout << "summarize_matched_version_11\t0" << std::endl;

  stats_tuple recall, precis;
  std::vector<double> B_frac_ones;

  if (!vm.count("no-expr")) {
    recall = do_it_all_wrapper(A_to_B, A_card, B_card, B_lengths, real_tau_B, readlen, NULL);
    precis = do_it_all_wrapper(B_to_A, B_card, A_card, A_lengths, real_tau_A, readlen, NULL);
    print_stats(precis, recall, "weighted_matched");
  }
                                                                    
  recall = do_it_all_wrapper(A_to_B, A_card, B_card, B_lengths, unif_tau_B, readlen, &B_frac_ones);
  precis = do_it_all_wrapper(B_to_A, B_card, A_card, A_lengths, unif_tau_A, readlen, NULL);
  print_stats(precis, recall, "unweighted_matched");

  std::ofstream f(vm["plot-output"].as<std::string>().c_str());
  BOOST_FOREACH(double x, B_frac_ones)
    f << x << std::endl;

  std::cerr << "Done!" << std::endl;
}
