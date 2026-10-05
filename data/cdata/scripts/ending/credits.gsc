#include maps\_endmission;

main()
{
    replacefunc(maps\_credit_h2_autogen::_id_B775, ::initiwcredits);
}

initcredits_h2mod()
{
    maps\_credits::addcenterimage("LOGO_H2MOD", 256, 64, 2);
    maps\_credits::addspace();
    // Fixed project names and contributor IDs are intentionally literal, so
    // this script update also works with an already-installed locale fastfile.
    maps\_credits::addcenterheading("H2-Mod-VR Developer");
    maps\_credits::addgap();
    maps\_credits::addcentername("Aeka0");
    maps\_credits::addspace();
    maps\_credits::addcenterheading("H2-Mod Developers");
    maps\_credits::addgap();
    maps\_credits::addcentername(&"CREDIT_H2MOD_VLAD");
    maps\_credits::addcenternamedouble(&"CREDIT_H2MOD_FUTURE", &"CREDIT_H2MOD_NETADR");
    maps\_credits::addgap();
    maps\_credits::addcenterheading(&"CREDIT_H2MOD_SPECIAL_THANKS");
    maps\_credits::addgap();
    maps\_credits::addcenternamedouble(&"CREDIT_H2MOD_SNAKE", &"CREDIT_H2MOD_QUAK");
    maps\_credits::addcenternamedouble(&"CREDIT_H2MOD_XENSIK", &"CREDIT_H2MOD_JARIK");
}

initiwcredits()
{
    initcredits_h2mod();
    maps\_credit_h2_autogen::initiwcredits_iw();
    maps\_credit_h2_autogen::initiwcredits_beenox();
    maps\_credit_h2_autogen::initiwcredits_ca();
    maps\_credit_h2_autogen::initiwcredits_nerve();
    maps\_credit_h2_autogen::initiwcredits_irongalaxy();
}
