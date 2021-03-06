#pragma once

#include <nano/secure/blockstore.hpp>
#include <nano/secure/common.hpp>
#include <nano/secure/ledger.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_set>

namespace nano
{
class channel;
class confirmation_solicitor;
class json_handler;
class node;
class vote_generator_session;
class vote_info final
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t sequence;
	nano::block_hash hash;
};
class election_vote_result final
{
public:
	election_vote_result () = default;
	election_vote_result (bool, bool);
	bool replay{ false };
	bool processed{ false };
};
enum class election_behavior
{
	normal,
	optimistic
};
struct election_cleanup_info final
{
	bool confirmed;
	nano::qualified_root root;
	nano::block_hash winner;
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks;
};

class election final : public std::enable_shared_from_this<nano::election>
{
	// Minimum time between broadcasts of the current winner of an election, as a backup to requesting confirmations
	std::chrono::milliseconds base_latency () const;
	std::function<void(std::shared_ptr<nano::block>)> confirmation_action;

private: // State management
	enum class state_t
	{
		passive, // only listening for incoming votes
		active, // actively request confirmations
		broadcasting, // request confirmations and broadcast the winner
		confirmed, // confirmed but still listening for votes
		expired_confirmed,
		expired_unconfirmed
	};
	static unsigned constexpr passive_duration_factor = 5;
	static unsigned constexpr active_request_count_min = 2;
	static unsigned constexpr confirmed_duration_factor = 5;
	std::atomic<nano::election::state_t> state_m = { state_t::passive };

	// These time points must be protected by this mutex
	std::mutex timepoints_mutex;
	std::chrono::steady_clock::time_point state_start = { std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point last_block = { std::chrono::steady_clock::now () };
	std::chrono::steady_clock::time_point last_req = { std::chrono::steady_clock::time_point () };

	bool valid_change (nano::election::state_t, nano::election::state_t) const;
	bool state_change (nano::election::state_t, nano::election::state_t);
	std::atomic<bool> prioritized_m = { false };

public: // State transitions
	bool transition_time (nano::confirmation_solicitor &);
	void transition_active ();

public: // Status
	bool confirmed () const;
	bool failed () const;
	bool prioritized () const;
	bool optimistic () const;
	std::shared_ptr<nano::block> winner ();

	void log_votes (nano::tally_t const &, std::string const & = "") const;
	nano::tally_t tally ();
	bool have_quorum (nano::tally_t const &, nano::uint128_t) const;

	nano::election_status status;
	unsigned confirmation_request_count{ 0 };

public: // Interface
	election (nano::node &, std::shared_ptr<nano::block>, std::function<void(std::shared_ptr<nano::block>)> const &, bool, nano::election_behavior);
	nano::election_vote_result vote (nano::account, uint64_t, nano::block_hash);
	bool publish (std::shared_ptr<nano::block> block_a);
	size_t insert_inactive_votes_cache (nano::block_hash const &);
	// Confirm this block if quorum is met
	void confirm_if_quorum ();
	void prioritize_election (nano::vote_generator_session &);
	nano::election_cleanup_info cleanup_info () const;

public: // Information
	uint64_t const height;
	nano::root const root;

private:
	void transition_active_impl ();
	void confirm_once (nano::election_status_type = nano::election_status_type::active_confirmed_quorum);
	void broadcast_block (nano::confirmation_solicitor &);
	void send_confirm_req (nano::confirmation_solicitor &);
	// Calculate votes for local representatives
	void generate_votes ();
	void remove_votes (nano::block_hash const &);

private:
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> last_blocks;
	std::unordered_map<nano::account, nano::vote_info> last_votes;
	std::unordered_map<nano::block_hash, nano::uint128_t> last_tally;

	nano::election_behavior const behavior{ nano::election_behavior::normal };
	std::chrono::steady_clock::time_point const election_start = { std::chrono::steady_clock::now () };

	nano::node & node;

	static std::chrono::seconds constexpr late_blocks_delay{ 5 };

	friend class active_transactions;
	friend class confirmation_solicitor;
	friend class json_handler;

public: // Only used in tests
	void force_confirm (nano::election_status_type = nano::election_status_type::active_confirmed_quorum);
	std::unordered_map<nano::account, nano::vote_info> votes ();
	std::unordered_map<nano::block_hash, std::shared_ptr<nano::block>> blocks ();

	friend class confirmation_solicitor_different_hash_Test;
	friend class confirmation_solicitor_bypass_max_requests_cap_Test;
	friend class votes_add_existing_Test;
	friend class votes_add_old_Test;
};
}
