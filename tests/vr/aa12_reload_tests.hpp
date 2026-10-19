#pragma once
#include "component/vr/gameplay/weapons/aa12/reload_profile.hpp"
#include "component/vr/gameplay/physical_reload_contact_sample.hpp"

namespace aa12_reload_tests
{
	template<class Fixture, class Check> void run(Check check)
	{
		namespace w = vr::gameplay::weapons;
		namespace p = w::physical_reload;
		namespace m = w::mechanics;
		using namespace vr::gameplay::hands;
		using namespace vr::gameplay::hands::pose_math;
		const auto& d = w::aa12::physical;
		constexpr float units = 39.37007874f;
		const quat mirror{1, 0, 0, 0};
		const auto pitch = [](float degrees) {
			const float half = degrees * .00872664626f;
			return quat{0, std::sin(half), 0, std::cos(half)};
		};
		for (int actor = 0; actor < 2; ++actor)
		for (const auto gun : {anchor{{}, {0, 0, 0, 1}},
		                       anchor{{120, -37, 91}, normalize({.2f, .5f, -.1f, .7f})}})
		{
			Fixture initial(&d);
			initial.owner.rear = vr::hand(1 - actor);
			p::presentation view;
			view.active = true; view.definition = &d; view.owner = initial.owner; view.ammo = initial.state;
			p::scene_frame scene;
			scene.owner = initial.owner; scene.definition = &d; scene.assembly = 1; scene.binding.valid = true;
			scene.binding.mirror = mirror; scene.binding.wrist = normalize({.3f, -.2f, .1f, .8f});
			vr::gameplay::hand_interaction::frame frame;
			frame.valid_hands = 3; frame.input = initial.input; frame.body.units_per_meter = units;
			frame.objects[0].owner = initial.owner; frame.objects[0].assembly = 1; frame.objects[0].gun = gun;
			const auto sample = [&](anchor controller) {
				frame.wrists[actor] = compose(gun, controller);
				check(p::sample_contact(scene, view, frame), "AA-12 uses current tracked wrists in either gun orientation");
				return scene.contact;
			};
			const auto grasp = w::select_magazine_grip(d, {0, 0, 0, 1}, actor, mirror, false, false, 0);
			const auto seated = compose(d.magazine_rest, inverse(grasp.in_wrist));
			const auto basis = w::magazine_wrist_basis(d, grasp, scene.binding.wrist, false);
			const anchor controller{seated.position, multiply(seated.rotation, conjugate(basis))};
			// Separate position and wrist-angle variations: rotating the authored
			// palm contact used to make the small magazine volume feel angle-gated.
			for (const auto delta : {vec{}, vec{.07f, 0, 0}, vec{-.07f, 0, 0},
			                         vec{0, .07f, 0}, vec{0, -.07f, 0}, vec{0, 0, -.07f}})
			for (float degrees : {0.f, -45.f, 45.f})
			{
				if (degrees != 0 && length(delta) != 0) continue;
				Fixture f(&d); f.owner = initial.owner; f.step();
				auto raw = controller;
				raw.position = add(raw.position, scale(delta, units));
				raw.rotation = multiply(pitch(degrees), raw.rotation);
				f.geometry = sample(raw);
				check(p::attached_magazine_contact(d.interaction, f.state, f.geometry),
				      "AA-12 magazine accepts nearby grabs and a turned wrist with either hand");
				check(f.geometry.slide_distance > d.interaction.slide_radius,
				      "AA-12 top handle cannot steal a magazine approach from below");
				const int total = m::total_rounds(f.state);
				f.trigger(true);
				check(f.control.magazine_grabbed() && !f.control.slide_held(), "AA-12 intended magazine grab wins");
				f.geometry.hand_in_gun[2] -= d.interaction.manual_magazine->pull_travel + .001f;
				f.step();
				check(!f.state.magazine_inserted && f.state.magazine_hand == vr::hand(actor) &&
				      m::total_rounds(f.state) == total, "AA-12 assisted extraction still requires a pull and conserves shells");
			}
			for (float degrees : {0.f, 75.f, 180.f})
			{
				// A contact 5 cm off-centre and 6.5 cm below the mouth is in the
				// enlarged well. Compare real sampled alignment, not a supplied cosine.
				anchor magazine{{}, multiply(d.magazine_rest.rotation, pitch(degrees))};
				const auto tip = compose(d.well, {scale({.05f, 0, -.065f}, units), {0, 0, 0, 1}}).position;
				magazine.position = sub(tip, rotate(magazine.rotation, d.magazine_top));
				auto raw = compose(magazine, inverse(grasp.in_wrist));
				raw.rotation = multiply(raw.rotation, conjugate(basis));
				const auto g = sample(raw);
				p::well_motion motion{{.05f, 0, -.12f}};
				check(p::advance_well(d.interaction, motion, g.magazine_top_in_well, g.insertion_alignment, false).insert ==
				      (degrees < 90), "AA-12 accepts oblique insertion at the wider mouth but rejects an inverted magazine");
			}
		}
		for (int actor = 0; actor < 2; ++actor)
		for (float travel : {0.f, d.interaction.slide_stroke})
		{
			const auto grip = actor ? pose_mirror::part(d.slide_grips[0], mirror) : d.slide_grips[0];
			const std::array<w::part_grip_pose, 1> grips{grip};
			const auto offset = scale(d.interaction.slide_axis, travel * units);
			for (const auto delta : {vec{}, vec{.09f, 0, 0}, vec{-.09f, 0, 0}, vec{0, .09f, 0},
			                         vec{0, -.09f, 0}, vec{0, 0, .09f}, vec{0, 0, -.04f}})
			{
				auto wrist = grip.wrist; wrist.position = add(wrist.position, add(offset, scale(delta, units)));
				const auto before = w::choose_part_grip(grips, wrist, offset, d.slide_grab_low, d.slide_grab_high, units);
				const auto after = w::choose_part_grip(grips, wrist, offset, d.slide_grab_low, d.slide_grab_high, units, d.slide_capture, actor);
				check(after.pose == before.pose && after.distance_meters == before.distance_meters &&
				      after.distance_meters < d.interaction.slide_radius,
				      "AA-12 keeps side/top handle assistance and modest downward reach at both travel limits");
			}
			auto below = grip.wrist; below.position = add(below.position, offset);
			below.position[2] = d.slide_grab_low[2] - .01f;
			check(w::choose_part_grip(grips, below, offset, d.slide_grab_low, d.slide_grab_high, units).distance_meters < d.interaction.slide_radius &&
			      w::choose_part_grip(grips, below, offset, d.slide_grab_low, d.slide_grab_high, units, d.slide_capture, actor).pose == w::no_part_grip,
			      "AA-12 lower boundary removes the old handle capture through the receiver");
		}
	}
}
