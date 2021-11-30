#!/usr/bin/env python3
import os
import sys
import requests

def send_mail(from_address, to_address, subject, body):
    print("Sending mail from " + from_address + " to " + to_address + " subject " + subject + " body " + body)
    mailgun_url = f"https://api.mailgun.net/v3/slateci.io/messages"
    r = requests.post(mailgun_url,
                         auth=("api", "b8bfdf9a540981781b3ec0c72f0e0834-fd0269a6-9282dca4"),
                         data={"from": from_address,
                               "to": to_address,
                               "subject": subject,
                               "text": body,
                               "html": body})
    if r.status_code != requests.codes.ok:
        sys.stderr.write(f"Can't send email got HTTP code {r.status_code}: {r.text}\n")


if __name__ == '__main__':
    if (len(sys.argv) == 5):
        from_address = sys.argv[1]
        to_address = sys.argv[2]
        subject = sys.argv[3]
        body = sys.argv[4]
        send_mail(from_address, to_address, subject, body)
    else:
        print("Send e-mail using mailgun")
        print("")
        print("      " + sys.argv[0] + " FROM_ADDRESS TO_ADDRESS SUBJECT BODY")
        print("")
        print("Example:  " + sys.argv[0] + " no-replay@slateci.io user@uni.edu Warning 'Your quota is 95% full'")


