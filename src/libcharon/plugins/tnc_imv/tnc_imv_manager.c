/*
 * Copyright (C) 2006 Mike McCauley
 * Copyright (C) 2010 Andreas Steffen, HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "tnc_imv_manager.h"
#include "tnc_imv_recommendations.h"

#include <tnc/imv/imv_manager.h>
#include <tnc/tncifimv.h>

#include <debug.h>
#include <daemon.h>
#include <threading/mutex.h>

typedef struct private_tnc_imv_manager_t private_tnc_imv_manager_t;


/**
 * Private data of an imv_manager_t object.
 */
struct private_tnc_imv_manager_t {

	/**
	 * Public members of imv_manager_t.
	 */
	imv_manager_t public;

	/**
	 * Linked list of IMVs
	 */
	linked_list_t *imvs;

	/**
	 * Next IMV ID to be assigned
	 */
	TNC_IMVID next_imv_id;

	/**
	 * Policy defining how to derive final recommendation from individual ones
	 */
	recommendation_policy_t policy;
};

METHOD(imv_manager_t, add, bool,
	private_tnc_imv_manager_t *this, imv_t *imv)
{
	TNC_Version version;

	/* Initialize the IMV module */
	imv->set_id(imv, this->next_imv_id);
	if (imv->initialize(imv->get_id(imv), TNC_IFIMV_VERSION_1,
		TNC_IFIMV_VERSION_1, &version) != TNC_RESULT_SUCCESS)
	{
		DBG1(DBG_TNC, "IMV \"%s\" failed to initialize", imv->get_name(imv));
		return FALSE;
	}
	this->imvs->insert_last(this->imvs, imv);
	this->next_imv_id++;

	if (imv->provide_bind_function(imv->get_id(imv), TNC_TNCS_BindFunction)
			!= TNC_RESULT_SUCCESS)
	{
		DBG1(DBG_TNC, "IMV \"%s\" could failed to obtain bind function",
					   imv->get_name(imv));
		this->imvs->remove_last(this->imvs, (void**)&imv);
		return FALSE;
	}

	return TRUE;
}

METHOD(imv_manager_t, remove_, imv_t*,
	private_tnc_imv_manager_t *this, TNC_IMVID id)
{
	enumerator_t *enumerator;
	imv_t *imv, *removed_imv = NULL;

	enumerator = this->imvs->create_enumerator(this->imvs);
	while (enumerator->enumerate(enumerator, &imv))
	{
		if (id == imv->get_id(imv))
		{
			this->imvs->remove_at(this->imvs, enumerator);
			removed_imv = imv;
			break;
		}
	}
	enumerator->destroy(enumerator);

	return removed_imv;
}

METHOD(imv_manager_t, is_registered, bool,
	private_tnc_imv_manager_t *this, TNC_IMVID id)
{
	enumerator_t *enumerator;
	imv_t *imv;
	bool found = FALSE;

	enumerator = this->imvs->create_enumerator(this->imvs);
	while (enumerator->enumerate(enumerator, &imv))
	{
		if (id == imv->get_id(imv))
		{
			found = TRUE;
			break;
		}
	}
	enumerator->destroy(enumerator);

	return found;
}

METHOD(imv_manager_t, get_recommendation_policy, recommendation_policy_t,
	private_tnc_imv_manager_t *this)
{
	return this->policy;
}

METHOD(imv_manager_t, create_recommendations, recommendations_t*,
	private_tnc_imv_manager_t *this)
{
	return tnc_imv_recommendations_create(this->imvs);
}

METHOD(imv_manager_t, enforce_recommendation, bool,
	private_tnc_imv_manager_t *this, TNC_IMV_Action_Recommendation rec,
									 TNC_IMV_Evaluation_Result eval)
{
	char *group;
	identification_t *id;
	ike_sa_t *ike_sa;
	auth_cfg_t *auth;
	bool no_access = FALSE;

	DBG1(DBG_TNC, "final recommendation is '%N' and evaluation is '%N'",
		 TNC_IMV_Action_Recommendation_names, rec,
		 TNC_IMV_Evaluation_Result_names, eval);

	switch (rec)
	{
		case TNC_IMV_ACTION_RECOMMENDATION_ALLOW:
			group = "allow";
			break;
		case TNC_IMV_ACTION_RECOMMENDATION_ISOLATE:
			group = "isolate";
			break;
		case TNC_IMV_ACTION_RECOMMENDATION_NO_ACCESS:
		case TNC_IMV_ACTION_RECOMMENDATION_NO_RECOMMENDATION:
		default:
			group = "no access";
			no_access = TRUE;
			break;
	}

	ike_sa = charon->bus->get_sa(charon->bus);
	if (!ike_sa)
	{
		DBG1(DBG_TNC, "policy enforcement point did not find IKE_SA");
		return FALSE;
	}

	id = ike_sa->get_other_id(ike_sa);
	DBG0(DBG_TNC, "policy enforced on peer '%Y' is '%s'", id, group);

	if (no_access)
	{
		return FALSE;
	}
	else
	{	
		auth = ike_sa->get_auth_cfg(ike_sa, FALSE);
		id = identification_create_from_string(group);
		auth->add(auth, AUTH_RULE_GROUP, id);
		DBG1(DBG_TNC, "policy enforcement point added group membership '%s'",
			 group);
	}
	return TRUE;
}


METHOD(imv_manager_t, notify_connection_change, void,
	private_tnc_imv_manager_t *this, TNC_ConnectionID id,
									 TNC_ConnectionState state)
{
	enumerator_t *enumerator;
	imv_t *imv;

	enumerator = this->imvs->create_enumerator(this->imvs);
	while (enumerator->enumerate(enumerator, &imv))
	{
		if (imv->notify_connection_change)
		{
			imv->notify_connection_change(imv->get_id(imv), id, state);
		}
	}
	enumerator->destroy(enumerator);
}

METHOD(imv_manager_t, set_message_types, TNC_Result,
	private_tnc_imv_manager_t *this, TNC_IMVID id,
									 TNC_MessageTypeList supported_types,
									 TNC_UInt32 type_count)
{
	enumerator_t *enumerator;
	imv_t *imv;
	TNC_Result result = TNC_RESULT_FATAL;

	enumerator = this->imvs->create_enumerator(this->imvs);
	while (enumerator->enumerate(enumerator, &imv))
	{
		if (id == imv->get_id(imv))
		{
			imv->set_message_types(imv, supported_types, type_count);
			result = TNC_RESULT_SUCCESS;
			break;
		}
	}
	enumerator->destroy(enumerator);
	return result;
}

METHOD(imv_manager_t, solicit_recommendation, void,
	private_tnc_imv_manager_t *this, TNC_ConnectionID id)
{
	enumerator_t *enumerator;
	imv_t *imv;

	enumerator = this->imvs->create_enumerator(this->imvs);
	while (enumerator->enumerate(enumerator, &imv))
	{
		imv->solicit_recommendation(imv->get_id(imv), id);
	}
	enumerator->destroy(enumerator);
}

METHOD(imv_manager_t, receive_message, void,
	private_tnc_imv_manager_t *this, TNC_ConnectionID connection_id,
									 TNC_BufferReference message,
									 TNC_UInt32 message_len,
									 TNC_MessageType message_type)
{
	bool type_supported = FALSE;
	enumerator_t *enumerator;
	imv_t *imv;

	enumerator = this->imvs->create_enumerator(this->imvs);
	while (enumerator->enumerate(enumerator, &imv))
	{
		if (imv->receive_message && imv->type_supported(imv, message_type))
		{
			type_supported = TRUE;
			imv->receive_message(imv->get_id(imv), connection_id,
								 message, message_len, message_type);
		}
	}
	enumerator->destroy(enumerator);
	if (!type_supported)
	{
		DBG2(DBG_TNC, "message type 0x%08x not supported by any IMV", message_type);
	}
}

METHOD(imv_manager_t, batch_ending, void,
	private_tnc_imv_manager_t *this, TNC_ConnectionID id)
{
	enumerator_t *enumerator;
	imv_t *imv;

	enumerator = this->imvs->create_enumerator(this->imvs);
	while (enumerator->enumerate(enumerator, &imv))
	{
		if (imv->batch_ending)
		{
			imv->batch_ending(imv->get_id(imv), id);
		}
	}
	enumerator->destroy(enumerator);
}

METHOD(imv_manager_t, destroy, void,
	private_tnc_imv_manager_t *this)
{
	imv_t *imv;

	while (this->imvs->remove_last(this->imvs, (void**)&imv) == SUCCESS)
	{
		if (imv->terminate &&
			imv->terminate(imv->get_id(imv)) != TNC_RESULT_SUCCESS)
		{
			DBG1(DBG_TNC, "IMV \"%s\" not terminated successfully",
						   imv->get_name(imv));
		}
		imv->destroy(imv);
	}
	this->imvs->destroy(this->imvs);
	free(this);
}

/**
 * Described in header.
 */
imv_manager_t* tnc_imv_manager_create(void)
{
	private_tnc_imv_manager_t *this;
	recommendation_policy_t policy;

	INIT(this,
		.public = {
			.add = _add,
			.remove = _remove_, /* avoid name conflict with stdio.h */
			.is_registered = _is_registered,
			.get_recommendation_policy = _get_recommendation_policy,
			.create_recommendations = _create_recommendations,
			.enforce_recommendation = _enforce_recommendation,
			.notify_connection_change = _notify_connection_change,
			.set_message_types = _set_message_types,
			.solicit_recommendation = _solicit_recommendation,
			.receive_message = _receive_message,
			.batch_ending = _batch_ending,
			.destroy = _destroy,
		},
		.imvs = linked_list_create(),
		.next_imv_id = 1,
	);
	policy = enum_from_name(recommendation_policy_names,
				lib->settings->get_str(lib->settings,
					"charon.plugins.tnc-imv.recommendation_policy", "default"));
	this->policy = (policy != -1) ? policy : RECOMMENDATION_POLICY_DEFAULT;
	DBG1(DBG_TNC, "TNC recommendation policy is '%N'",
				   recommendation_policy_names, this->policy);

	return &this->public;
}
