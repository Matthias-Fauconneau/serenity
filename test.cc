#include "thread.h"
#include "data.h"

struct Test {
    Test() {
        array<char> output;
        TextData s = readFile(arguments()[0]);

        array<string> allUsers;
        do allUsers.append(s.word()); while(s.match(' '));
        s.skip('\n');

        map<string, float> currencies;
        while(!s.wouldMatch('#')) {
            string currency = s.word();
            s.skip(' ');
            float value = s.decimal();
            currencies.insert(currency, value);
            s.skip('\n');
        }

        map<string, map<string, float>> contributions;
        for(string user: allUsers) {
            contributions.insert(user);
            for(string currency: currencies.keys) contributions.at(user).insert(currency, 0);
        }
        map<string, map<string, float>> costs;
        for(string user: allUsers) {
            costs.insert(user);
            for(string currency: currencies.keys) costs.at(user).insert(currency, 0);
        }

        uint objectWidth = 16;
        uint entryWidth = objectWidth+1+4+1+3+1+9+1+4+1+3+1;
        array<char> header = repeat(" ", entryWidth);
        for(string user: allUsers) header.append(str(right(user,9)+' '));
        output.append(header+'\n');

        while(s) {
            if(s.match('#')) { s.line(); continue; }
            s.whileAny(' ');
            string object = s.until("  ");
            s.whileAny(' ');
            uint nativeAmount = s.integer();
            s.skip(' ');
            string currency = s.word();
            assert_(currencies.contains(currency));
            s.skip(' ');
            string paidBy = s.word();
            assert_(allUsers.contains(paidBy));
            //float amount = currencies.at(currency) * nativeAmount;

            array<string> entryUsers;
            if(s.match(" ->")) {
                while(s.match(' ')) {
                    string user = s.word();
                    assert_(allUsers.contains(user), user);
                    entryUsers.append(user);
                }
                assert_(entryUsers);
            }
            s.skip('\n');

            if(!entryUsers) entryUsers = copy(allUsers);

            contributions.at(paidBy).at(currency) += nativeAmount;
            for(string user: entryUsers) costs.at(user).at(currency) += float(nativeAmount)/float(entryUsers.size);

            array<char> status;
            status.append(right(object,objectWidth)+' ');
            status.append(right(str(nativeAmount),4)+' ');
            status.append(currency+' ');
            status.append(right(paidBy,9)+' ');
            status.append(right(str(contributions.at(paidBy).at(currency)),4)+' ');
            status.append(currency+' ');
            for(string user: allUsers) status.append(right(str(costs.at(user).at(currency),0u),9)+' ');
            output.append(status+'\n');
        }

        {
            output.append(header+'\n');
            output.append("Contributions\n");
            for(string currency: currencies.keys) {
                array<char> line = right(currency, entryWidth-1);
                line.append(' ');
                for(string user: allUsers) line.append(right(str(contributions.at(user).at(currency),0u),9)+' ');
                output.append(line+'\n');
            }
            {
                array<char> line = right(join(currencies.keys,"+"_), entryWidth-1);
                line.append(' ');
                for(string user: allUsers) {
                    float common = 0;
                    for(string currency: currencies.keys) common += currencies.at(currency) * contributions.at(user).at(currency);
                    line.append(right(str(common,0u),9)+' ');
                }
                output.append(line+'\n');
            }
            output.append("Costs\n");
            for(string currency: currencies.keys) {
                array<char> line = right(currency, entryWidth-1);
                line.append(' ');
                for(string user: allUsers) line.append(right(str(costs.at(user).at(currency),0u),9)+' ');
                output.append(line+'\n');
            }
            {
                array<char> line = right(join(currencies.keys,"+"_), entryWidth-1);
                line.append(' ');
                for(string user: allUsers) {
                    float common = 0;
                    for(string currency: currencies.keys) common += currencies.at(currency) * costs.at(user).at(currency);
                    line.append(right(str(common,0u),9)+' ');
                }
                output.append(line+'\n');
            }
            output.append("Balance\n");
            for(string currency: currencies.keys) {
                array<char> line = right(currency, entryWidth-1);
                line.append(' ');
                for(string user: allUsers) line.append(right(str(contributions.at(user).at(currency)-costs.at(user).at(currency),0u),9)+' ');
                output.append(line+'\n');
            }
            {
                array<char> line = right(join(currencies.keys,"+"_), entryWidth-1);
                line.append(' ');
                for(string user: allUsers) {
                    float common = 0;
                    for(string currency: currencies.keys) common += currencies.at(currency) * (contributions.at(user).at(currency)-costs.at(user).at(currency));
                    line.append(right(str(common,0u),9)+' ');
                }
                output.append(line+'\n');
            }
        }
        log(output);
        writeFile(arguments()[1], output);
    }
} app;

