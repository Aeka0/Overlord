#include "component/vr/hand.hpp"
using vr::hand;
#include "component/vr/gameplay/controller_locomotion.hpp"
#include "component/vr/gameplay/weapons/m9/profile.hpp"
#include "component/vr/gameplay/controller_buttons.hpp"
#include "component/vr/gameplay/controller_firing.hpp"
#include "component/vr/gameplay/shot_geometry.hpp"
#include "component/vr/gameplay/weapon_hud_policy.hpp"
#include "component/vr/gameplay/weapon_feedback.hpp"
#include "component/vr/gameplay/weapons/m9/feedback.hpp"
#include "controller_orientation_tests.hpp"
#include "controller_pose_pipeline_tests.hpp"
#include "controller_profile_retry_tests.hpp"
#include "controller_pose_quality_tests.hpp"
#include "input_history_tests.hpp"
#include "input_manifest_tests.hpp"
#include "slow_simulation_input_tests.hpp"
#include "sentry_input_tests.hpp"
#include "controller_stance_tests.hpp"
#include "controller_ads_tests.hpp"
#include "vehicle_locomotion_tests.hpp"
#include "remote_look_tests.hpp"
#include "fixed_sniper_tests.hpp"
#include "fixed_sniper_hand_aim_tests.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

int main()
{
	using namespace vr::controller_input;
	int failures{};
	const auto check = [&](const bool value, const char* name)
	{
		if (!value) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
	};
	const auto now = clock::now();
	controller_orientation_tests(check);
	controller_pose_pipeline_tests::run(check);
	controller_profile_retry_tests(check);
	controller_pose_quality_tests(check);
	input_history_tests(check);
	input_manifest_tests(check);
	controller_stance_tests(check);
	controller_ads_tests(check);
	vehicle_locomotion_tests(check);
	remote_look_tests(check);
	fixed_sniper_tests(check);
	fixed_sniper_hand_aim_tests(check);
	slow_simulation_input_tests(check);
	sentry_input_tests(check);
	{
		using namespace vr::controller_haptics;
		mailbox haptics;
		frame f{};
		f.sequence = f.reference_generation = 1; f.focused = true; f.sampled_at = now;
		for (int h = 0; h < 2; ++h) f.grip[h].valid = f.aim[h].valid = true;
		haptics.push(0, {.03f, 120, .3f, 1, now});
		haptics.push(0, {.03f, 120, .1f, 1, now});
		haptics.push(1, {1, 1000, 2, 1, now});
		auto out = haptics.take(f);
		check(out[0].amplitude == .3f && out[1].amplitude == 1 && out[1].seconds == .1f && out[1].frequency == 320,
			"independent physical haptics coalesce and clamp");
		check(haptics.take(f)[0].amplitude == 0, "haptic delivery consumes once");
		for (int mode = 0; mode < 4; ++mode)
		{
			auto invalid = f;
			haptics.push(0, {.03f, 120, .3f, 1, now});
			if (mode == 0) invalid.focused = false;
			if (mode == 1) invalid.reference_generation++;
			if (mode == 2) invalid.grip[0].valid = false;
			if (mode == 3) invalid.sampled_at += std::chrono::milliseconds(101);
			check(haptics.take(invalid)[0].amplitude == 0 && haptics.take(f)[0].amplitude == 0,
				"invalid haptic discarded without delayed replay");
		}
		haptics.push(-1, {.03f, 120, .3f, 1, now});
		haptics.push(0, {.03f, 120, std::numeric_limits<float>::quiet_NaN(), 1, now});
		check(haptics.take(f)[0].amplitude == 0, "malformed haptics rejected");
		using namespace vr::gameplay::weapons;
		hold owner{49, 1, hand::right, hand::none, hold_source::engine_default, 1};
		feedback::event e{mechanics::effect::magazine_in, owner, 1, now, {}, nullptr};
		check(feedback::fresh(e, owner, f, true, now), "matching confirmed event deliverable");
		check(!feedback::fresh(e, owner, f, false, now), "menu suppresses feedback");
		owner.rear_revision++;
		check(!feedback::fresh(e, owner, f, true, now), "new weapon ownership rejects prior event");
		owner.rear=hand::none;owner.support=hand::left;e.owner=owner;
		check(feedback::fresh(e,owner,f,true,now),"confirmed magazine feedback survives foregrip-only carry");
		e.kind=mechanics::effect::shot;
		check(!feedback::fresh(e,owner,f,true,now),"foregrip-only carry cannot receive firing feedback");
		e.kind=mechanics::effect::action_grab;owner.support=hand::right;
		check(!feedback::fresh(e,owner,f,true,now),"moving the holding hand rejects stale manipulation feedback");
		check(m9::sound_key(mechanics::effect::shot) == nullptr,
			"confirmed shots retain native sound without duplicate playback");
	}
	frame input{};
	input.sequence = 1;
	input.reference_generation = 1;
	input.sampled_at = now;
	input.focused = input.move_active = input.turn_active = true;
	locomotion policy;
	const auto read = [&](bool gameplay = true)
	{
		return policy.consume(input, gameplay, 0.2f, {turn_mode::snap}, now);
	};
	input.move = {0, 1};
	check(!read().active, "initial deflection cannot arm");
	input.move = {};
	check(!read().active, "neutral arms without movement");
	input.move = {0, 1};
	check(std::abs(read().forward - 1) < 0.001f, "forward");
	check(read().right == 0, "forward stays native-relative; HMD yaw belongs to game view");
	input.move = {1, 1};
	const auto diagonal = read();
	check(std::abs(std::hypot(diagonal.forward, diagonal.right) - 1) < 0.001f, "diagonal normalized");
	input.turn = {1, 0};
	check(read().yaw_delta == -30, "right turn sign");
	check(read().yaw_delta == 0, "held stick does not repeat even same sample");
	input.turn = {-1, 0};
	check(read().yaw_delta == 0, "crossing directly does not rearm");
	input.turn = {};
	(void)read();
	input.turn = {-1, 0};
	check(read().yaw_delta == 30, "return neutral rearms");
	check(!read(false).active && !read().active, "menu then deflected resume");
	input.move = input.turn = {};
	(void)read();
	check(read().active, "rearmed");
	input.reference_generation++;
	input.move = {0, 1};
	check(!read().active, "recenter requires neutral");
	input.move = {};
	(void)read();
	input.sampled_at = now - std::chrono::milliseconds(151);
	check(!read().active, "stale snapshot rejected");
	input.sampled_at = now + std::chrono::milliseconds(1);
	check(!read().active, "future snapshot rejected");
	input.sampled_at = now;
	input.move[0] = std::numeric_limits<float>::quiet_NaN();
	check(!read().active, "NaN rejected");
	input.move = {};
	(void)read();
	input.focused = false;
	check(!read().active, "focus lost");
	input.focused = true;
	input.move = {0, 1};
	check(!read().active, "focus resume needs neutral");
	// Smooth turning integrates command time, not publication count or render FPS.
	for (const auto rate : {30, 72, 90, 120, 144})
	{
		locomotion smooth;
		frame sample{};
		sample.focused = sample.move_active = sample.turn_active = true;
		sample.sequence = sample.reference_generation = 1;
		sample.sampled_at = now;
		(void)smooth.consume(sample, true, 0.2f, {}, now);
		float total{};
		for (int step = 1; step <= rate; ++step)
		{
			const auto tick = now + std::chrono::nanoseconds(1'000'000'000LL * step / rate);
			sample.sampled_at = tick;
			sample.turn = {1, 0};
			const auto value = smooth.consume(sample, true, 0.2f, {}, tick);
			total += value.yaw_delta;
			check(!value.discontinuous_turn, "smooth does not reset temporal history");
		}
		check(std::abs(total + 90.0f) < 0.001f, "smooth speed independent of command rate");
	}
	locomotion smooth;
	input = {};
	input.focused = input.move_active = input.turn_active = true;
	input.sequence = input.reference_generation = 1;
	input.sampled_at = now;
	(void)smooth.consume(input, true, 0.2f, {}, now);
	const auto later = now + std::chrono::milliseconds(20);
	input.sampled_at = later;
	input.turn = {-0.6f, 0};
	check(std::abs(smooth.consume(input, true, 0.2f, {}, later).yaw_delta - 0.9f) < 0.001f,
		"smooth partial deflection and left sign");
	check(smooth.consume(input, true, 0.2f, {}, later).yaw_delta == 0, "same timestamp does not double integrate");
	check(!smooth.consume(input, true, 0.2f, {turn_mode::snap}, later).active,
		"switch to snap while held requires neutral");
	input.turn = {};
	(void)smooth.consume(input, true, 0.2f, {turn_mode::snap}, later);
	input.turn = {1, 0};
	check(smooth.consume(input, true, 0.2f, {turn_mode::snap}, later).discontinuous_turn,
		"snap still works after mode switch");
	check(!smooth.consume(input, true, 0.2f, {}, later).active, "switch back to smooth requires neutral");
	input.turn = {};
	(void)smooth.consume(input, true, 0.2f, {}, later);
	input.turn = {1, 0};
	input.sampled_at = now + std::chrono::seconds(1);
	check(!smooth.consume(input, true, 0.2f, {}, input.sampled_at).active,
		"long command gap cannot catch up rotation");
	input.turn = {};
	(void)smooth.consume(input, true, 0.2f, {}, input.sampled_at);
	input.sampled_at += std::chrono::milliseconds(100);
	input.turn = {1, 0};
	check(std::abs(smooth.consume(input, true, 0.2f, {}, input.sampled_at).yaw_delta + 4.5f) < 0.001f,
		"short hitch integration capped at 50 ms");
	input.focused = false;
	check(!smooth.consume(input, true, 0.2f, {}, input.sampled_at).active, "smooth loses focus");
	input.focused = true;
	check(!smooth.consume(input, true, 0.2f, {}, input.sampled_at).active,
		"smooth held resume cannot rotate");
	// Digital actions are independent of analog stick activity/neutral rearming.
	command_buttons buttons;
	digital_sampler sprint_sampler, jump_sampler;
	frame digital{};
	digital.sequence = digital.reference_generation = 1;
	digital.focused = true;
	digital.sampled_at = now;
	button_commands button_result;
	const auto sample_buttons = [&](bool sprint, bool jump, bool active = true)
	{
		digital.sprint = sprint_sampler.sample(active, sprint, digital.sampled_at);
		digital.jump = jump_sampler.sample(active, jump, digital.sampled_at);
		++digital.sequence;
	};
	const auto read_buttons = [&](bool gameplay = true)
	{
		button_result = buttons.consume(digital, gameplay, digital.sampled_at);
		return button_result.held;
	};
	sample_buttons(true, true);
	check(read_buttons() == 0 && read_buttons() == 0, "initial held clicks blocked");
	check(button_result.pressed == 0, "initial held jump cannot notify a script");
	sample_buttons(false, false);
	check(read_buttons() == 0, "button release arms independently of axes");
	sample_buttons(true, false);
	check(read_buttons() == sprint_button, "left click maps only sprint");
	check(button_result.pressed == sprint_button, "sprint edge cannot emit a jump notification");
	check(read_buttons() == sprint_button, "held state preserved on repeated command reads");
	check(button_result.pressed == 0, "held action does not repeat script notification");
	sample_buttons(true, true);
	check(read_buttons() == (sprint_button | jump_button), "simultaneous actions");
	check(button_result.pressed == jump_button, "new jump has one edge while sprint remains held");
	sample_buttons(false, true);
	check(read_buttons() == jump_button, "right click independent of sprint release");
	check(button_result.pressed == 0, "new render frame with held jump cannot notify again");
	sample_buttons(false, false);
	check(read_buttons() == 0, "release clears controller request");
	sample_buttons(true, true);
	sample_buttons(false, false); // Both transitions before command thread reads.
	check(read_buttons() == (sprint_button | jump_button), "short taps survive skipped render samples");
	check(button_result.pressed == (sprint_button | jump_button),
		"released short jump tap still reaches script notification path");
	check(read_buttons() == 0, "completed taps consumed only once");
	check(button_result.pressed == 0, "rereading short jump tap cannot replay its notification");
	sample_buttons(false, true);
	sample_buttons(false, false);
	check(read_buttons() == jump_button && button_result.pressed == jump_button,
		"second completed jump tap has its own script notification");
	sample_buttons(false, true);
	check(read_buttons() == jump_button && button_result.pressed == jump_button,
		"new press immediately after a completed tap is not lost to held-mask edge detection");
	sample_buttons(false, false);
	(void)read_buttons();
	sample_buttons(true, true);
	sample_buttons(false, false);
	sample_buttons(true, true);
	sample_buttons(false, false);
	check(read_buttons() == (sprint_button | jump_button) && read_buttons() == 0,
		"multiple unread taps coalesce without catch-up queue");
	check(read_buttons(false) == 0, "menu blocks buttons");
	check(button_result.pressed == 0, "menu blocks script notifications");
	sample_buttons(true, true);
	check(read_buttons() == 0, "held menu resume blocked");
	check(button_result.pressed == 0, "held menu resume cannot notify a script");
	sample_buttons(false, false);
	(void)read_buttons();
	sample_buttons(true, true);
	digital.focused = false;
	check(read_buttons() == 0, "focus loss blocks buttons");
	digital.focused = true;
	check(read_buttons() == 0, "held focus resume blocked");
	check(button_result.pressed == 0, "held focus resume cannot notify a script");
	sample_buttons(false, false);
	(void)read_buttons();
	sample_buttons(true, true, false);
	sample_buttons(true, true); // Runtime discontinuity missed by command reader.
	check(read_buttons() == 0, "unobserved binding or runtime loss cannot replay press");
	check(button_result.pressed == 0, "unobserved runtime loss cannot replay script notification");
	sample_buttons(false, false);
	(void)read_buttons();
	digital.sprint = sprint_sampler.sample(false, false, now);
	digital.jump = jump_sampler.sample(true, true, now);
	check(read_buttons() == jump_button, "unbound sprint cannot disable jump");
	++digital.reference_generation;
	check(read_buttons() == 0, "recenter while held requires release");
	check(button_result.pressed == 0, "recenter discards pending script notification");
	sample_buttons(false, false);
	(void)read_buttons();
	sample_buttons(true, true);
	check(buttons.consume(digital, true, now + std::chrono::milliseconds(151)).held == 0,
		"stale digital frame blocked");
	check(read_buttons() == 0, "stale recovery still held blocked");
	check(button_result.pressed == 0, "stale recovery cannot notify a script");
	sample_buttons(false, false);
	(void)read_buttons();
	sample_buttons(true, true);
	check(buttons.consume(digital, true, now - std::chrono::milliseconds(1)).held == 0,
		"future digital frame blocked");
	sample_buttons(false, false);
	(void)read_buttons();
	digital.sampled_at += std::chrono::milliseconds(151);
	sample_buttons(true, true);
	check(read_buttons() == 0, "long runtime and command gap requires release");
	check(button_result.pressed == 0, "command gap discards pending script notification");
	sample_buttons(false, false);
	(void)read_buttons();
	sample_buttons(true, true);
	digital.sequence = 1;
	check(read_buttons() == 0, "sequence rollback cannot replay clicks");
	check(button_result.pressed == 0, "sequence rollback cannot replay script notification");
	sample_buttons(false, false);
	(void)read_buttons();
	sample_buttons(true, true);
	buttons.reset();
	check(read_buttons() == 0, "suppressed command branch reset blocks held click");
	check(button_result.pressed == 0, "suppressed command branch discards script notification");
	check((0x81 | sprint_button | jump_button) == 0x483, "native unrelated bits retained by OR");
	using namespace vr::gameplay::weapons;
	holding_state holding;
	check(!holding.current().can_fire(), "empty hands cannot fire");
	auto owner = holding.equipped(49);
	check(owner.can_fire() && owner.rear == hand::right, "native adapter bootstraps one rear owner");
	check(holding.equipped(49).revision == owner.revision, "same equipment is idempotent");
	check(!holding.grip(owner, grip_role::support, hand::right), "rear owner cannot also be support");
	check(holding.grip(owner, grip_role::support, hand::left), "support grip recorded separately");
	check(!holding.grip(owner, grip_role::rear, hand::left), "stale grip transaction rejected");
	owner = holding.current();
	check(holding.grip(owner, grip_role::rear, hand::left), "rear ownership transfers left");
	owner = holding.current();
	check(owner.rear == hand::left && owner.support == hand::none, "transfer clears duplicate support");
	check(holding.equipped(49).rear == hand::left, "native adapter does not override interaction");
	check(holding.grip(owner, grip_role::rear, hand::none), "release rear grip");
	check(!holding.equipped(49).can_fire(), "same equipment must not regrab released weapon");
	owner = holding.equipped(50);
	check(owner.rear == hand::right && owner.source == hold_source::engine_default, "new weapon boots native owner");
	check(!holding.grip(owner, grip_role::rear, static_cast<hand>(9)), "invalid hand rejected");
	check(!holding.grip(owner, static_cast<grip_role>(9), hand::left), "invalid grip rejected");
	check(!holding.equipped(0).can_fire(), "unequip invalidates authority");
	owner = holding.equipped(49);
	frame fire_input{};
	fire_input.focused = true;
	fire_input.sequence = 1;
	fire_input.reference_generation = 2;
	fire_input.sampled_at = now;
	for (int i = 0; i < 2; ++i) fire_input.grip[i].valid = fire_input.aim[i].valid = true;
	std::array<digital_sampler, 2> trigger_samples;
	const auto sample_triggers = [&](bool left, bool right, bool active = true) {
		fire_input.trigger[0] = trigger_samples[0].sample(active, left, now);
		fire_input.trigger[1] = trigger_samples[1].sample(active, right, now);
		++fire_input.sequence;
	};
	trigger_policy trigger;
	for (int type:{-1,0,1,2,3,4,5,999})for (bool independent:{false,true})
	{
		const auto delivery=native_fire_delivery(type);
		check(suppress_native_attack(delivery,independent)==(independent && type!=3),
			"owned presentation leaves launchers on native attack while suppressing duplicate bullets and unknown types");
		check((delivery!=fire_delivery::unsupported)==(type==1 || type==3),
			"native fire accepts bullet/projectile contracts without admitting thrown ordnance");
	}
	const auto fire = [&](bool gameplay = true, bool valid_muzzle = true) {
		return trigger.consume(fire_input, owner, gameplay, valid_muzzle, now);
	};
	sample_triggers(true, true);
	check(!fire(), "held trigger on initial attach blocked");
	sample_triggers(false, false); check(!fire(), "release arms trigger");
	sample_triggers(true, false); check(!fire(), "nonholding hand cannot fire");
	sample_triggers(true, true); check(fire() && fire(), "owner held trigger preserves native automatic cadence");
	{
		trigger_policy device;
		sample_triggers(false,false);check(!device.consume(fire_input,owner,true,true,now,true),"neutral device trigger arms without firing");
		sample_triggers(false,true);check(device.consume(fire_input,owner,true,true,now,true) && !device.consume(fire_input,owner,true,true,now,true),"designator confirmation is one command per deliberate press");
		check(!device.consume(fire_input,owner,true,false,now,true) && !device.consume(fire_input,owner,true,true,now,true),"holding through cooldown cannot fire automatically when readiness returns");
		sample_triggers(false,false);device.consume(fire_input,owner,true,true,now,true);
		sample_triggers(false,true);check(device.consume(fire_input,owner,true,true,now,true),"fresh press after cooldown can designate again");
	}
	check(!fire(false), "menu suppresses fire");
	check(!fire(), "held trigger after menu waits for release");
	sample_triggers(false, false); (void)fire();
	sample_triggers(false, true); check(fire(), "deliberate owner press fires again");
	check(!fire(true, false) && !fire(), "lost muzzle and held recovery cannot fire");
	sample_triggers(false, false); (void)fire();
	sample_triggers(true, true);
	check(holding.grip(owner, grip_role::rear, hand::left), "transfer during held triggers");
	owner = holding.current();
	check(!fire(), "new rear owner must release first");
	sample_triggers(false, false); (void)fire();
	sample_triggers(false, true); check(!fire(), "old owner loses firing authority");
	sample_triggers(true, false); check(fire(), "left rear trigger fires");
	owner = holding.equipped(50);
	sample_triggers(true, true); check(!fire(), "weapon switch held trigger blocked");
	sample_triggers(false, false); (void)fire();
	sample_triggers(false, true);
	check(!trigger.consume(fire_input, owner, true, true, now + std::chrono::milliseconds(151)), "stale fire input blocked");
	check(!fire(), "stale recovery requires release");
	sample_triggers(false, false); (void)fire();
	sample_triggers(false, true);
	fire_input.aim[1].valid = false;
	check(!fire(), "owner pose invalid blocks fire");
	fire_input.aim[1].valid = true; check(!fire(), "pose recovery held blocked");
	sample_triggers(false, false); (void)fire();
	sample_triggers(false, true); sample_triggers(false, false);
	check(fire() && !fire(), "completed quick tap emitted once");
	sample_triggers(false, true);
	++fire_input.reference_generation; check(!fire(), "recenter held trigger blocked");
	sample_triggers(false, false); (void)fire();
	sample_triggers(false, true);
	fire_input.focused = false; check(!fire(), "dashboard blocks fire");
	fire_input.focused = true; check(!fire(), "dashboard held recovery blocked");
	sample_triggers(false, false); (void)fire();
	sample_triggers(false, true, false); check(!fire(), "disconnected action blocked");
	sample_triggers(false, true); check(!fire(), "reconnect held trigger blocked");
	sample_triggers(false, false); (void)fire();
	sample_triggers(false, true); fire_input.sequence = 1;
	check(!fire(), "fire sequence rollback blocked");
	check((jump_button | attack_button | 0x10) == 0x411, "attack OR retains jump and reload");
	muzzle_frame pose{true, owner, fire_input.reference_generation, 9, now, now,
		{100, 200, 300}, {{{0,1,0},{-1,0,0},{0,0,1}}}};
	check(ready(pose, owner, fire_input.reference_generation, now), "valid world muzzle ready");
	const auto shot = geometry(pose);
	pose.model = {true, {1,2,3}, {10,20,30}};
	check(ready(pose, owner, fire_input.reference_generation, now) && geometry(pose).origin == shot.origin,
		"model presentation placement never changes world shot geometry or readiness");
	check(shot.forward == pose.axis[0] && shot.up == pose.axis[2] && shot.origin == pose.position &&
		shot.right == vr::gameplay::hands::vec{1,0,0}, "native right is negative H2 left; origin not camera");
	check(!ready(pose, owner, fire_input.reference_generation + 1, now), "muzzle recenter mismatch rejected");
	auto other = owner; ++other.revision;
	check(!ready(pose, other, fire_input.reference_generation, now), "muzzle old ownership rejected");
	check(!ready(pose, owner, fire_input.reference_generation, now + std::chrono::milliseconds(151)), "stale muzzle rejected");
	check(!ready(pose, owner, fire_input.reference_generation, now - std::chrono::milliseconds(1)), "future muzzle rejected");
	auto bad_pose = pose; bad_pose.axis[1] = bad_pose.axis[0];
	check(!valid_geometry(bad_pose), "parallel muzzle basis rejected");
	bad_pose = pose; bad_pose.axis[2][2] = -1;
	check(!valid_geometry(bad_pose), "reflected muzzle basis rejected");
	bad_pose = pose; bad_pose.position[0] = std::numeric_limits<float>::quiet_NaN();
	check(!valid_geometry(bad_pose), "NaN muzzle rejected");
	bad_pose = pose; bad_pose.axis[0][1] = std::numeric_limits<float>::infinity();
	check(!valid_geometry(bad_pose), "infinite basis rejected");
	vr::gameplay::weapon_hud::toggle_policy hud;
	std::array<digital_sampler, 2> primary_samples;
	frame hud_input{};
	hud_input.focused = true; hud_input.sequence = 1;
	hud_input.reference_generation = 1; hud_input.sampled_at = now;
	for (auto& grip : hud_input.grip) grip.valid = true;
	owner = holding.equipped(60);
	const auto sample_primary = [&](bool left, bool right, bool active = true) {
		hud_input.primary[0] = primary_samples[0].sample(active, left, now);
		hud_input.primary[1] = primary_samples[1].sample(active, right, now);
		++hud_input.sequence;
	};
	const auto toggle = [&](bool gameplay = true) { return hud.consume(hud_input, owner, gameplay, now); };
	sample_primary(false, true);
	check(!toggle() && hud.visible(), "HUD initial held A does not hide");
	sample_primary(false, false); check(!toggle(), "HUD release arms A");
	sample_primary(true, false); check(!toggle(), "HUD off-hand X ignored");
	sample_primary(true, true); check(toggle() && !hud.visible(), "HUD owner A hides");
	check(!toggle() && !hud.visible(), "HUD repeated command does not retoggle");
	sample_primary(false, false); (void)toggle();
	sample_primary(false, true); sample_primary(false, false);
	check(toggle() && hud.visible(), "HUD quick tap toggles once");
	sample_primary(false, true); check(!toggle(false), "HUD menu input suppressed");
	check(!toggle() && hud.visible(), "HUD held menu recovery waits for release");
	sample_primary(false, false); (void)toggle();
	sample_primary(false, true); hud_input.focused = false;
	check(!toggle(), "HUD dashboard input suppressed");
	hud_input.focused = true; check(!toggle(), "HUD dashboard held recovery suppressed");
	sample_primary(false, false); (void)toggle();
	sample_primary(false, true); hud_input.grip[1].valid = false;
	check(!toggle(), "HUD lost grip blocks toggle");
	hud_input.grip[1].valid = true; check(!toggle(), "HUD tracking recovery rearms");
	sample_primary(false, false); (void)toggle();
	sample_primary(false, true); ++hud_input.reference_generation;
	check(!toggle(), "HUD recenter discards pending press");
	sample_primary(false, false); (void)toggle();
	sample_primary(false, true);
	check(!hud.consume(hud_input, owner, true, now + std::chrono::milliseconds(151)), "HUD stale input blocked");
	check(!toggle(), "HUD stale held recovery blocked");
	sample_primary(false, false); (void)toggle();
	sample_primary(false, true, false); check(!toggle(), "HUD disconnected action ignored");
	sample_primary(false, true); check(!toggle(), "HUD reconnect held blocked");
	sample_primary(false, false); (void)toggle();
	sample_primary(false, true); hud_input.sequence = 1;
	check(!toggle(), "HUD sequence rollback ignored");
	check(holding.grip(owner, grip_role::rear, hand::left), "HUD ownership transfer setup");
	owner = holding.current();
	sample_primary(true, true); check(!toggle(), "HUD new owner held X rearms");
	sample_primary(false, false); (void)toggle();
	sample_primary(false, true); check(toggle() && !hud.visible(), "global A closes HUD with only a left-hand weapon");
	sample_primary(true, false); check(!toggle() && !hud.visible(), "X does not override the shared HUD preference");
	check(holding.grip(owner, grip_role::support, hand::right), "HUD support grip setup");
	owner = holding.current();
	sample_primary(false, false); (void)toggle();
	sample_primary(false, true); check(toggle() && hud.visible(), "global A reopens after support-grip changes");
	owner = holding.equipped(61); check(!toggle(), "HUD weapon change discards held button");
	owner.rear = static_cast<hand>(9); check(!toggle(), "HUD malformed owner rejected before indexing");
	for (const bool initially_closed : {false, true})
	{
		vr::gameplay::weapon_hud::toggle_policy latch;
		frame input{};
		input.sequence = input.reference_generation = 1; input.sampled_at = now; input.focused = true;
		input.grip[1].valid = true; input.primary[1].active = true; input.primary[1].generation = 1;
		vr::gameplay::weapons::hold rear{7, 1, hand::right, hand::none, hold_source::engine_default, 1};
		(void)latch.consume(input, rear, true, now);
		if (initially_closed)
		{
			input.primary[1].down = true; ++input.primary[1].presses; ++input.sequence;
			check(latch.consume(input, rear, true, now), "latch closes only by button");
		}
		const bool expected = !initially_closed;
		(void)latch.consume(input, rear, false, now); // pause / suppressed command
		input.focused = false; input.grip[1].valid = false;
		(void)latch.consume(input, rear, true, now);
		latch.reset_input(); // command polling gap
		(void)latch.consume(input, {}, true, now); // weapon dropped / holstered
		check(latch.visible() == expected, "empty hands preserve both HUD toggle preferences");
		input.focused = true; input.grip[1].valid = true;
		++rear.weapon; ++rear.rear_revision; ++input.reference_generation;
		(void)latch.consume(input, rear, true, now);
		(void)latch.consume(input, rear, true, now + std::chrono::seconds(60));
		check(latch.visible() == expected, "pause/tracking/switch/recenter/timeout preserve shown AND closed states");
	}
	std::cout << "controller input failures=" << failures << '\n';
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
